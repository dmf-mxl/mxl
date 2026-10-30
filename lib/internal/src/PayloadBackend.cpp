// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#ifndef _GNU_SOURCE
#   define _GNU_SOURCE
#endif

#include "mxl-internal/PayloadBackend.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>
#include <dlfcn.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fmt/format.h>
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    namespace
    {
        /** Prefix of every backend library file name. */
        constexpr auto BACKEND_LIBRARY_PREFIX = std::string_view{"libmxl-payload-"};

        /** Suffix of every backend library file name. CMake gives MODULE libraries this suffix on Linux and macOS. */
        constexpr auto BACKEND_LIBRARY_SUFFIX = std::string_view{".so"};

        /** Longest accepted backend name. */
        constexpr auto MAX_BACKEND_NAME_LENGTH = std::size_t{64};

        /** Storage types built into libmxl. Their names cannot be used by a backend library. */
        constexpr auto BUILT_IN_STORAGE_NAME_HOST = std::string_view{"host"};

        /**
         * A function that is part of the binary holding the loader. Its address locates that binary.
         */
        void locationAnchor() noexcept
        {}

        /**
         * Find the directory of the binary that contains this code, which is libmxl in a normal build.
         * \return The directory, or an empty path if it cannot be determined.
         */
        std::filesystem::path findOwnDirectory() noexcept
        {
            try
            {
                auto info = Dl_info{};
                // Converting a function pointer to void* is conditionally supported. It is required on POSIX
                // systems, which are the only systems MXL runs on.
                if ((::dladdr(reinterpret_cast<void const*>(&locationAnchor), &info) != 0) && (info.dli_fname != nullptr))
                {
                    return std::filesystem::weakly_canonical(std::filesystem::path{info.dli_fname}).parent_path();
                }
            }
            catch (std::exception const& e)
            {
                MXL_ERROR("Could not resolve the directory of libmxl: {}", e.what());
                return {};
            }
            // The default loader then searches only the directories of the plugin path variable.
            MXL_ERROR("Could not determine the directory of libmxl. Payload backends are only searched for in {}.",
                PayloadBackendLoader::PAYLOAD_PLUGIN_PATH_VARIABLE);
            return {};
        }

        /**
         * Read an environment variable, unless the process runs with raised privileges.
         * \param[in] name The variable name.
         * \return The value, or null if the variable is not set or the process is setuid or setgid.
         */
        char const* getenvUnlessPrivileged(char const* name) noexcept
        {
#ifdef __APPLE__
            return (::issetugid() != 0) ? nullptr : std::getenv(name);
#else
            return ::secure_getenv(name);
#endif
        }

        /**
         * \return The search directories of the default loader.
         */
        std::vector<std::filesystem::path> defaultSearchDirectories()
        {
            auto directories = std::vector<std::filesystem::path>{};
            if (auto const* const value = getenvUnlessPrivileged(PayloadBackendLoader::PAYLOAD_PLUGIN_PATH_VARIABLE); value != nullptr)
            {
                directories = PayloadBackendLoader::parsePluginPath(value);
            }
            if (auto ownDirectory = findOwnDirectory(); !ownDirectory.empty())
            {
                directories.push_back(std::move(ownDirectory));
            }
            return directories;
        }

        /**
         * Check a backend function table against the name it was loaded under.
         * \param[in] api The table returned by the backend.
         * \param[in] name The name the backend was loaded under.
         * \return An empty string if the table is valid, otherwise a description of the problem.
         */
        std::string validateApi(mxlPayloadBackendApiV1 const& api, std::string_view name)
        {
            // exportDescriptors and importDescriptors were added at the end. A backend built before them reports a
            // smaller table and simply does not share file descriptors.
            constexpr auto minStructSize = offsetof(mxlPayloadBackendApiV1, exportDescriptors);
            if (api.structSize < minStructSize)
            {
                return fmt::format("function table is {} bytes, expected at least {}", api.structSize, minStructSize);
            }
            if (api.abiVersion != MXL_PAYLOAD_BACKEND_ABI_VERSION)
            {
                return fmt::format("implements interface version {}, expected {}", api.abiVersion, MXL_PAYLOAD_BACKEND_ABI_VERSION);
            }
            if ((api.name == nullptr) || (name != api.name))
            {
                return fmt::format("reports name '{}'", (api.name != nullptr) ? api.name : "<null>");
            }
            if ((api.storageType != MXL_PAYLOAD_STORAGE_HOST_POINTER) && (api.storageType != MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER))
            {
                return fmt::format("reports unknown storage type {}", api.storageType);
            }
            if ((api.prepare == nullptr) || (api.open == nullptr) || (api.destroy == nullptr) || (api.deviceIndex == nullptr) ||
                (api.slotStorage == nullptr) || (api.describeLayout == nullptr) || (api.mapSlots == nullptr))
            {
                return "does not provide every function";
            }
            return {};
        }

        /** \return True when the table is large enough to hold the file-descriptor functions. */
        bool reportsDescriptorFunctions(mxlPayloadBackendApiV1 const& api) noexcept
        {
            return api.structSize >= sizeof(mxlPayloadBackendApiV1);
        }

        /**
         * Ask a writer backend for the descriptors it shares.
         * \return The descriptors, owned by the caller. Empty when the backend shares none.
         */
        std::vector<int> takeExportedDescriptors(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendInstance instance)
        {
            if (!reportsDescriptorFunctions(api) || (api.exportDescriptors == nullptr))
            {
                return {};
            }
            auto count = std::uint32_t{0};
            if (auto const status = api.exportDescriptors(instance, &count, nullptr); status != MXL_STATUS_OK)
            {
                throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to export its storage descriptors.", api.name)};
            }
            if (count == 0U)
            {
                return {};
            }
            auto fds = std::vector<int>(count, -1);
            if (auto const status = api.exportDescriptors(instance, &count, fds.data()); status != MXL_STATUS_OK)
            {
                throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to export its storage descriptors.", api.name)};
            }
            fds.resize(count);
            return fds;
        }

        /**
         * Receive the descriptors of a flow from its payload socket.
         * \param[in] socketPath The published path of payload.sock.
         * \return The received descriptors. The caller owns them.
         */
        std::vector<int> receiveDescriptors(std::filesystem::path const& socketPath)
        {
            auto const path = socketPath.string();
            auto address = sockaddr_un{};
            address.sun_family = AF_UNIX;
            if ((path.size() + 1U) > sizeof(address.sun_path))
            {
                throw PayloadStorageError{MXL_ERR_INVALID_ARG, "The payload socket path is too long."};
            }
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);

            auto const connection = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (connection < 0)
            {
                throw PayloadStorageError{MXL_ERR_UNKNOWN, "Could not create a socket for the payload descriptors."};
            }
            if (::connect(connection, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
            {
                ::close(connection);
                throw PayloadStorageError{MXL_ERR_UNKNOWN, fmt::format("Could not connect to the payload socket '{}'.", path)};
            }

            auto byte = char{0};
            auto iov = iovec{};
            iov.iov_base = &byte;
            iov.iov_len = 1;
            alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 253U)] = {};
            auto message = msghdr{};
            message.msg_iov = &iov;
            message.msg_iovlen = 1;
            message.msg_control = control;
            message.msg_controllen = sizeof(control);
            auto const received = ::recvmsg(connection, &message, MSG_CMSG_CLOEXEC);
            ::close(connection);
            if (received < 0)
            {
                throw PayloadStorageError{MXL_ERR_UNKNOWN, "Could not receive the payload descriptors."};
            }

            auto fds = std::vector<int>{};
            for (auto* header = CMSG_FIRSTHDR(&message); header != nullptr; header = CMSG_NXTHDR(&message, header))
            {
                if ((header->cmsg_level != SOL_SOCKET) || (header->cmsg_type != SCM_RIGHTS))
                {
                    continue;
                }
                auto const bytes = static_cast<std::size_t>(header->cmsg_len) - CMSG_LEN(0);
                auto const count = bytes / sizeof(int);
                auto const* data = reinterpret_cast<int const*>(CMSG_DATA(header));
                fds.insert(fds.end(), data, data + count);
            }
            if (fds.empty())
            {
                throw PayloadStorageError{MXL_ERR_UNKNOWN, "The payload socket sent no descriptors."};
            }
            return fds;
        }
    }

    /**
     * Serves a writer's storage descriptors on the payload socket in the flow directory.
     * The listening socket is bound before the flow directory is renamed, so the published path reaches the same inode.
     */
    struct PayloadFdServer
    {
        /** \param[in] publishedPath Path readers connect to, after the flow directory is published. */
        /** \param[in] bindPath Path to bind now, in the staging directory. */
        /** \param[in] fds Descriptors to hand out. This object owns them. */
        PayloadFdServer(std::filesystem::path publishedPath, std::filesystem::path bindPath, std::vector<int> fds)
            : _publishedPath{std::move(publishedPath)}
            , _bindPath{std::move(bindPath)}
            , _fds{std::move(fds)}
            , _listen{-1}
            , _stop{false}
        {
            auto const path = _bindPath.string();
            auto address = sockaddr_un{};
            address.sun_family = AF_UNIX;
            if ((path.size() + 1U) > sizeof(address.sun_path))
            {
                throw PayloadStorageError{MXL_ERR_INVALID_ARG, "The payload socket path is too long."};
            }
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
            ::unlink(path.c_str());

            _listen = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if ((_listen < 0) || (::bind(_listen, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) || (::listen(_listen, 8) != 0))
            {
                if (_listen >= 0)
                {
                    ::close(_listen);
                    _listen = -1;
                }
                for (auto const fd : _fds)
                {
                    if (fd >= 0)
                    {
                        ::close(fd);
                    }
                }
                _fds.clear();
                ::unlink(path.c_str());
                throw PayloadStorageError{MXL_ERR_UNKNOWN, fmt::format("Could not listen on the payload socket '{}'.", path)};
            }
            applySocketMode();
            try
            {
                _thread = std::thread{[this] { serve(); }};
            }
            catch (...)
            {
                ::close(_listen);
                _listen = -1;
                for (auto const fd : _fds)
                {
                    if (fd >= 0)
                    {
                        ::close(fd);
                    }
                }
                _fds.clear();
                ::unlink(path.c_str());
                throw;
            }
        }

        ~PayloadFdServer()
        {
            _stop.store(true);
            if (_listen >= 0)
            {
                ::shutdown(_listen, SHUT_RDWR);
            }
            if (_thread.joinable())
            {
                _thread.join();
            }
            if (_listen >= 0)
            {
                ::close(_listen);
            }
            for (auto const fd : _fds)
            {
                if (fd >= 0)
                {
                    ::close(fd);
                }
            }
            ::unlink(_publishedPath.c_str());
            ::unlink(_bindPath.c_str());
        }

        PayloadFdServer(PayloadFdServer const&) = delete;
        PayloadFdServer& operator=(PayloadFdServer const&) = delete;

    private:
        /** Match the socket's write permission to the flow directory's read permission. */
        void applySocketMode()
        {
            struct stat info{};
            if (::stat(_bindPath.parent_path().c_str(), &info) != 0)
            {
                return;
            }
            auto mode = mode_t{S_IRUSR | S_IWUSR};
            if ((info.st_mode & S_IRGRP) != 0)
            {
                mode |= S_IRGRP | S_IWGRP;
            }
            if ((info.st_mode & S_IROTH) != 0)
            {
                mode |= S_IROTH | S_IWOTH;
            }
            (void)::fchmod(_listen, mode);
        }

        /** \return True when the connected process may read this flow directory. */
        bool peerMayRead(int connection) const
        {
            auto credentials = ucred{};
            auto length = socklen_t{sizeof(credentials)};
            struct stat info{};
            if ((::getsockopt(connection, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) || (::fstat(_listen, &info) != 0))
            {
                return false;
            }
            if (credentials.uid == info.st_uid)
            {
                return true;
            }
            if (((info.st_mode & S_IWGRP) != 0) && (credentials.gid == info.st_gid))
            {
                return true;
            }
            return (info.st_mode & S_IWOTH) != 0;
        }

        /** Accept readers until the writer storage is destroyed. */
        void serve()
        {
            while (!_stop.load())
            {
                auto ready = pollfd{};
                ready.fd = _listen;
                ready.events = POLLIN;
                if (::poll(&ready, 1, 200) <= 0)
                {
                    continue;
                }
                auto const connection = ::accept(_listen, nullptr, nullptr);
                if (connection < 0)
                {
                    continue;
                }
                if (peerMayRead(connection))
                {
                    sendDescriptors(connection);
                }
                ::close(connection);
            }
        }

        /** Send a duplicate of each stored descriptor. */
        void sendDescriptors(int connection) const
        {
            auto copies = std::vector<int>{};
            copies.reserve(_fds.size());
            for (auto const fd : _fds)
            {
                auto const copy = ::dup(fd);
                if (copy < 0)
                {
                    for (auto const opened : copies)
                    {
                        ::close(opened);
                    }
                    return;
                }
                copies.push_back(copy);
            }

            auto byte = char{0};
            auto iov = iovec{};
            iov.iov_base = &byte;
            iov.iov_len = 1;
            auto control = std::vector<char>(CMSG_SPACE(sizeof(int) * copies.size()));
            auto message = msghdr{};
            message.msg_iov = &iov;
            message.msg_iovlen = 1;
            message.msg_control = control.data();
            message.msg_controllen = control.size();
            auto* header = CMSG_FIRSTHDR(&message);
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = CMSG_LEN(sizeof(int) * copies.size());
            std::memcpy(CMSG_DATA(header), copies.data(), sizeof(int) * copies.size());
            (void)::sendmsg(connection, &message, MSG_NOSIGNAL);
            for (auto const copy : copies)
            {
                ::close(copy);
            }
        }

        std::filesystem::path _publishedPath;
        std::filesystem::path _bindPath;
        std::vector<int> _fds;
        int _listen;
        std::atomic<bool> _stop;
        std::thread _thread;
    };

    PayloadBackendLoader& PayloadBackendLoader::defaultLoader()
    {
        static auto loader = PayloadBackendLoader{defaultSearchDirectories()};
        return loader;
    }

    PayloadBackendLoader::PayloadBackendLoader(std::vector<std::filesystem::path> searchDirectories)
        : _searchDirectories{std::move(searchDirectories)}
        , _mutex{}
        , _backends{}
    {}

    PayloadBackendLoader::~PayloadBackendLoader() = default;

    mxlPayloadBackendApiV1 const& PayloadBackendLoader::load(std::string const& name)
    {
        if (!isValidBackendName(name))
        {
            throw std::invalid_argument{fmt::format("Invalid payload backend name '{}'.", name)};
        }
        if (name == BUILT_IN_STORAGE_NAME_HOST)
        {
            throw std::invalid_argument{"The host payload storage is built into libmxl and is not loaded from a library."};
        }

        auto const lock = std::lock_guard{_mutex};
        if (auto const pos = _backends.find(name); pos != _backends.end())
        {
            return *pos->second;
        }

        auto const fileName = libraryFileName(name);
        auto const found = std::ranges::find_if(_searchDirectories,
            [&](std::filesystem::path const& directory)
            {
                auto ec = std::error_code{};
                return std::filesystem::is_regular_file(directory / fileName, ec);
            });
        if (found == _searchDirectories.end())
        {
            auto searched = std::string{};
            for (auto const& directory : _searchDirectories)
            {
                searched += (searched.empty() ? "" : ", ") + directory.string();
            }
            throw std::runtime_error{
                fmt::format("Cannot find payload backend '{}' ({}) in: {}.", name, fileName, searched.empty() ? "no directories" : searched)};
        }

        auto const path = *found / fileName;
        // The handle is intentionally never closed. Function tables and the code they point to must stay valid
        // for storage objects that may outlive any particular caller.
        auto* const handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            auto const* const error = ::dlerror();
            throw std::runtime_error{
                fmt::format("Cannot load payload backend '{}' from {}: {}", name, path.string(), (error != nullptr) ? error : "unknown error")};
        }

        auto* const symbol = ::dlsym(handle, MXL_PAYLOAD_BACKEND_ENTRY_POINT);
        if (symbol == nullptr)
        {
            ::dlclose(handle);
            throw std::runtime_error{fmt::format("Payload backend library {} does not export {}.", path.string(), MXL_PAYLOAD_BACKEND_ENTRY_POINT)};
        }

        // Converting the object pointer returned by dlsym to a function pointer is required to work on POSIX systems.
        auto const entryPoint = reinterpret_cast<mxlGetPayloadBackendApiFn>(symbol);
        mxlPayloadBackendApiV1 const* api = nullptr;
        if (auto const status = entryPoint(MXL_PAYLOAD_BACKEND_ABI_VERSION, &api); (status != MXL_STATUS_OK) || (api == nullptr))
        {
            ::dlclose(handle);
            throw std::runtime_error{
                fmt::format("Payload backend library {} does not implement interface version {}.", path.string(), MXL_PAYLOAD_BACKEND_ABI_VERSION)};
        }

        if (auto const problem = validateApi(*api, name); !problem.empty())
        {
            ::dlclose(handle);
            throw std::runtime_error{fmt::format("Payload backend library {} is invalid: it {}.", path.string(), problem)};
        }

        MXL_DEBUG("Loaded payload backend '{}' from {}", name, path.string());
        _backends.emplace(name, api);
        return *api;
    }

    std::vector<std::filesystem::path> const& PayloadBackendLoader::searchDirectories() const noexcept
    {
        return _searchDirectories;
    }

    std::vector<std::filesystem::path> PayloadBackendLoader::parsePluginPath(std::string_view value)
    {
        auto directories = std::vector<std::filesystem::path>{};
        while (!value.empty())
        {
            auto const end = value.find(':');
            auto const entry = std::filesystem::path{value.substr(0, end)};
            if (entry.is_absolute())
            {
                directories.push_back(entry);
            }
            else if (!entry.empty())
            {
                MXL_WARN("Ignoring relative directory '{}' in {}.", entry.string(), PAYLOAD_PLUGIN_PATH_VARIABLE);
            }
            value = (end == std::string_view::npos) ? std::string_view{} : value.substr(end + 1);
        }
        return directories;
    }

    bool PayloadBackendLoader::isValidBackendName(std::string_view name) noexcept
    {
        auto const isLowerOrDigit = [](char c)
        {
            return ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9'));
        };

        if (name.empty() || (name.size() > MAX_BACKEND_NAME_LENGTH) || !isLowerOrDigit(name.front()))
        {
            return false;
        }
        return std::ranges::all_of(name, [&](char c) { return isLowerOrDigit(c) || (c == '-'); });
    }

    std::string PayloadBackendLoader::libraryFileName(std::string_view name)
    {
        return fmt::format("{}{}{}", BACKEND_LIBRARY_PREFIX, name, BACKEND_LIBRARY_SUFFIX);
    }

    std::unique_ptr<PluginPayloadStorage> PluginPayloadStorage::prepare(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendFlowInfo const& info,
        std::string const& options, std::filesystem::path const& stagingDir, std::filesystem::path const& finalDir)
    {
        mxlPayloadBackendInstance instance = nullptr;
        if (auto const status = api.prepare(&info, options.empty() ? "{}" : options.c_str(), stagingDir.c_str(), finalDir.c_str(), &instance);
            status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to prepare the flow storage.", api.name)};
        }
        auto storage = std::unique_ptr<PluginPayloadStorage>{new PluginPayloadStorage{api, instance, info.grainCount}};
        auto fds = takeExportedDescriptors(api, instance);
        if (!fds.empty())
        {
            storage->serveDescriptors(finalDir / "payload.sock", stagingDir / "payload.sock", std::move(fds));
        }
        return storage;
    }

    std::unique_ptr<PluginPayloadStorage> PluginPayloadStorage::open(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendFlowInfo const& info,
        std::string const& options, std::filesystem::path const& flowDir)
    {
        mxlPayloadBackendInstance instance = nullptr;
        if (auto const status = api.open(&info, options.empty() ? "{}" : options.c_str(), flowDir.c_str(), &instance); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to open the flow storage.", api.name)};
        }
        auto storage = std::unique_ptr<PluginPayloadStorage>{new PluginPayloadStorage{api, instance, info.grainCount}};
        if (reportsDescriptorFunctions(api) && (api.importDescriptors != nullptr))
        {
            storage->_descriptorSocket = flowDir / "payload.sock";
        }
        return storage;
    }

    void PluginPayloadStorage::serveDescriptors(std::filesystem::path socketPath, std::filesystem::path bindPath, std::vector<int> fds)
    {
        _descriptorSocket = std::move(socketPath);
        _fdServer.reset(new PayloadFdServer{_descriptorSocket, std::move(bindPath), std::move(fds)});
    }

    PluginPayloadStorage::PluginPayloadStorage(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendInstance instance, std::size_t slotCount) noexcept
        : _api{&api}
        , _instance{instance}
        , _slotCount{slotCount}
        , _descriptorSocket{}
        , _fdServer{}
    {}

    PluginPayloadStorage::~PluginPayloadStorage()
    {
        _fdServer.reset();
        _api->destroy(_instance);
    }

    mxlPayloadStorageType PluginPayloadStorage::type() const noexcept
    {
        return static_cast<mxlPayloadStorageType>(_api->storageType);
    }

    std::int32_t PluginPayloadStorage::deviceIndex() const noexcept
    {
        return _api->deviceIndex(_instance);
    }

    std::size_t PluginPayloadStorage::slotCount() const noexcept
    {
        return _slotCount;
    }

    mxlGrainStorage PluginPayloadStorage::slotStorage(std::size_t slot) const
    {
        if (slot >= _slotCount)
        {
            throw std::out_of_range{fmt::format("Slot {} is out of range, the storage has {} slots.", slot, _slotCount)};
        }

        auto result = mxlGrainStorage{};
        std::memset(&result, 0, sizeof(result));
        if (auto const status = _api->slotStorage(_instance, static_cast<std::uint32_t>(slot), &result); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot provide the storage of slot {}.", _api->name, slot)};
        }

        // Check that the backend filled the description it was asked for.
        if ((result.version != 1U) || (result.size != sizeof(mxlGrainStorage)) || (result.storageType != _api->storageType) || (result.slot != slot))
        {
            throw PayloadStorageError{
                MXL_ERR_INTERNAL, fmt::format("Payload backend '{}' returned an invalid description of slot {}.", _api->name, slot)};
        }
        return result;
    }

    void PluginPayloadStorage::describeLayout(mxlGrainStorageLayout& layout) const
    {
        if (auto const status = _api->describeLayout(_instance, &layout); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot describe the storage layout.", _api->name)};
        }
    }

    void PluginPayloadStorage::mapSlots() const
    {
        // The writer already holds the memory it exported. Only a reader fetches descriptors.
        if (reportsDescriptorFunctions(*_api) && (_api->importDescriptors != nullptr) && !_descriptorSocket.empty() && (_fdServer == nullptr))
        {
            auto fds = receiveDescriptors(_descriptorSocket);
            auto const status = _api->importDescriptors(_instance, static_cast<std::uint32_t>(fds.size()), fds.data());
            if (status != MXL_STATUS_OK)
            {
                throw PayloadStorageError{status, fmt::format("Payload backend '{}' rejected the storage descriptors.", _api->name)};
            }
        }
        if (auto const status = _api->mapSlots(_instance); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot map the flow storage.", _api->name)};
        }
    }

    UnavailablePayloadStorage::UnavailablePayloadStorage(mxlPayloadStorageType type, std::size_t slotCount, mxlStatus status, std::string reason)
        : _type{type}
        , _slotCount{slotCount}
        , _status{status}
        , _reason{std::move(reason)}
    {}

    mxlPayloadStorageType UnavailablePayloadStorage::type() const noexcept
    {
        return _type;
    }

    std::int32_t UnavailablePayloadStorage::deviceIndex() const noexcept
    {
        return -1;
    }

    std::size_t UnavailablePayloadStorage::slotCount() const noexcept
    {
        return _slotCount;
    }

    mxlGrainStorage UnavailablePayloadStorage::slotStorage(std::size_t) const
    {
        throw PayloadStorageError{_status, _reason};
    }

    void UnavailablePayloadStorage::describeLayout(mxlGrainStorageLayout&) const
    {
        throw PayloadStorageError{_status, _reason};
    }

    void UnavailablePayloadStorage::mapSlots() const
    {
        throw PayloadStorageError{_status, _reason};
    }
}
