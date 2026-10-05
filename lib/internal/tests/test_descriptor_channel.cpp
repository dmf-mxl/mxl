// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "mxl-internal/DescriptorChannel.hpp"
#include "../../tests/Utils.hpp"

using namespace mxl::lib;
namespace fs = std::filesystem;

namespace
{
    /**
     * Removes a directory tree when it goes out of scope.
     */
    struct TempDirectory
    {
        TempDirectory()
            : path{mxl::tests::makeTempDomain()}
        {}

        ~TempDirectory()
        {
            auto ec = std::error_code{};
            fs::remove_all(path, ec);
        }

        TempDirectory(TempDirectory const&) = delete;
        TempDirectory(TempDirectory&&) = delete;
        TempDirectory& operator=(TempDirectory const&) = delete;
        TempDirectory& operator=(TempDirectory&&) = delete;

        fs::path path; ///< The directory.
    };

    /**
     * \param[in] count How many files to open.
     * \return Descriptors of distinct temporary files.
     */
    std::vector<UniqueFd> openFiles(std::size_t count)
    {
        auto result = std::vector<UniqueFd>{};
        for (auto i = std::size_t{0}; i < count; ++i)
        {
            auto file = std::string{"/tmp/mxl-descriptor-test-XXXXXX"};
            auto fd = UniqueFd{::mkstemp(file.data())};
            REQUIRE(fd.get() >= 0);
            ::unlink(file.c_str());
            result.push_back(std::move(fd));
        }
        return result;
    }

    /**
     * \param[in] lhs A descriptor.
     * \param[in] rhs Another descriptor.
     * \return true if both refer to the same file.
     */
    bool sameFile(int lhs, int rhs)
    {
        struct stat a{};
        struct stat b{};
        return (::fstat(lhs, &a) == 0) && (::fstat(rhs, &b) == 0) && (a.st_dev == b.st_dev) && (a.st_ino == b.st_ino);
    }

    /**
     * Create the file whose read permission the socket follows, as the flow's data file.
     * \param[in] directory Where to create it.
     * \param[in] mode Its permissions.
     * \return Its path.
     */
    fs::path makeReference(fs::path const& directory, mode_t mode)
    {
        auto const path = directory / "data";
        auto const fd = UniqueFd{::open(path.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600)};
        REQUIRE(fd.get() >= 0);
        REQUIRE(::fchmod(fd.get(), mode) == 0);
        return path;
    }

    /**
     * \param[in] path A file.
     * \return Its permission bits.
     */
    mode_t permissionsOf(fs::path const& path)
    {
        struct stat info{};
        REQUIRE(::lstat(path.c_str(), &info) == 0);
        return info.st_mode & 0777;
    }

    /**
     * Leave a socket file that nobody listens on, as a writer that died does.
     * \param[in] path Where to bind it.
     */
    void bindStaleSocket(fs::path const& path)
    {
        auto const fd = UniqueFd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
        REQUIRE(fd.get() >= 0);
        auto address = sockaddr_un{};
        address.sun_family = AF_UNIX;
        REQUIRE(path.string().size() < sizeof(address.sun_path));
        std::memcpy(address.sun_path, path.c_str(), path.string().size() + 1U);
        REQUIRE(::bind(fd.get(), reinterpret_cast<sockaddr const*>(&address), sizeof(address)) == 0);
    }

    /**
     * \param[in] files Descriptors.
     * \return Their numbers.
     */
    std::vector<int> numbers(std::vector<UniqueFd> const& files)
    {
        auto result = std::vector<int>{};
        for (auto const& file : files)
        {
            result.push_back(file.get());
        }
        return result;
    }
}

TEST_CASE("Descriptor channel: a reader receives every descriptor, in order", "[descriptor channel]")
{
    auto const directory = TempDirectory{};
    auto const socketPath = directory.path / PAYLOAD_SOCKET_FILE_NAME;

    // More descriptors than one message can carry.
    auto const files = openFiles(300);
    auto const server = DescriptorServer{socketPath, numbers(files), makeReference(directory.path, 0644)};

    for (auto attempt = 0; attempt < 2; ++attempt)
    {
        auto const received = receiveDescriptors(socketPath);
        REQUIRE(received.size() == files.size());
        for (auto i = std::size_t{0}; i < files.size(); ++i)
        {
            REQUIRE(received[i].get() != files[i].get());
            REQUIRE(sameFile(received[i].get(), files[i].get()));
            REQUIRE((::fcntl(received[i].get(), F_GETFD) & FD_CLOEXEC) != 0);
        }
    }
}

TEST_CASE("Descriptor channel: whoever may read the flow may connect", "[descriptor channel]")
{
    // Connecting needs write permission on the socket. It is given where the reference has read permission.
    auto const [referenceMode, socketMode] = GENERATE(table<mode_t, mode_t>({
        {0644, 0666},
        {0640, 0660},
        {0604, 0606},
        {0600, 0600},
        {0400, 0600},
    }));
    auto const directory = TempDirectory{};
    auto const socketPath = directory.path / PAYLOAD_SOCKET_FILE_NAME;
    auto const server = DescriptorServer{socketPath, {}, makeReference(directory.path, referenceMode)};

    REQUIRE(permissionsOf(socketPath) == socketMode);
}

TEST_CASE("Descriptor channel: an empty set", "[descriptor channel]")
{
    auto const directory = TempDirectory{};
    auto const socketPath = directory.path / PAYLOAD_SOCKET_FILE_NAME;
    auto const server = DescriptorServer{socketPath, {}, makeReference(directory.path, 0644)};

    REQUIRE(receiveDescriptors(socketPath).empty());
}

#ifdef __linux__
TEST_CASE("Descriptor channel: paths longer than a socket address", "[descriptor channel]")
{
    auto const directory = TempDirectory{};
    auto const longDirectory = directory.path / std::string(120, 'd');
    REQUIRE(fs::create_directory(longDirectory));
    auto const socketPath = longDirectory / PAYLOAD_SOCKET_FILE_NAME;
    REQUIRE(socketPath.string().size() > 108U);

    auto const files = openFiles(3);
    auto const server = DescriptorServer{socketPath, numbers(files), makeReference(longDirectory, 0644)};
    auto const received = receiveDescriptors(socketPath);
    REQUIRE(received.size() == 3U);
    REQUIRE(sameFile(received[2].get(), files[2].get()));
}
#endif

TEST_CASE("Descriptor channel: the socket survives renaming its directory", "[descriptor channel]")
{
    // A flow is published by renaming its temporary directory.
    auto const directory = TempDirectory{};
    auto const staging = directory.path / ".mxl-tmp-test";
    auto const published = directory.path / "flow.mxl-flow";
    REQUIRE(fs::create_directory(staging));

    auto const files = openFiles(2);
    {
        auto const server = DescriptorServer{staging / PAYLOAD_SOCKET_FILE_NAME, numbers(files), makeReference(staging, 0644)};
        fs::rename(staging, published);

        auto const received = receiveDescriptors(published / PAYLOAD_SOCKET_FILE_NAME);
        REQUIRE(received.size() == 2U);
        REQUIRE(sameFile(received[0].get(), files[0].get()));
    }

    // The server removes its socket from where the directory went.
    REQUIRE_FALSE(fs::exists(fs::symlink_status(published / PAYLOAD_SOCKET_FILE_NAME)));
}

TEST_CASE("Descriptor channel: no server", "[descriptor channel]")
{
    auto const directory = TempDirectory{};
    auto const socketPath = directory.path / PAYLOAD_SOCKET_FILE_NAME;
    auto const reference = makeReference(directory.path, 0644);

    // No socket at all.
    REQUIRE_THROWS_AS(receiveDescriptors(socketPath), std::system_error);

    // A server that stopped removes its socket.
    {
        auto const server = DescriptorServer{socketPath, {}, reference};
    }
    REQUIRE_FALSE(fs::exists(fs::symlink_status(socketPath)));
    REQUIRE_THROWS_AS(receiveDescriptors(socketPath), std::system_error);

    // A writer that died leaves its socket, on which nobody listens.
    bindStaleSocket(socketPath);
    REQUIRE_THROWS_AS(receiveDescriptors(socketPath), std::system_error);

    // The path is taken, so a second server cannot bind it.
    REQUIRE_THROWS_AS(DescriptorServer(socketPath, {}, reference), std::system_error);
}

TEST_CASE("Descriptor channel: a server removes only its own socket", "[descriptor channel]")
{
    auto const directory = TempDirectory{};
    auto const socketPath = directory.path / PAYLOAD_SOCKET_FILE_NAME;
    {
        auto const server = DescriptorServer{socketPath, {}, makeReference(directory.path, 0644)};

        // Another writer's socket takes the path.
        fs::remove(socketPath);
        bindStaleSocket(socketPath);
    }
    REQUIRE(fs::exists(fs::symlink_status(socketPath)));
}
