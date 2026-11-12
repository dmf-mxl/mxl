// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/DescriptorChannel.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fmt/format.h>
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    namespace
    {
        /** Identifies the messages of the channel. The characters "MXLD" in little-endian order. */
        constexpr auto CHANNEL_MAGIC = std::uint32_t{0x444C584DU};

        /** Version of the channel protocol. */
        constexpr auto CHANNEL_VERSION = std::uint32_t{1U};

        /** Most descriptors in one message. The kernel accepts up to 253 (SCM_MAX_FD). */
        constexpr auto DESCRIPTORS_PER_MESSAGE = std::size_t{250U};

        /** How long one send or receive on a connection may block. */
        constexpr auto IO_TIMEOUT_SECONDS = 5;

#ifdef __linux__
        /** Flags for opening the directory of the socket, only to find the socket in it. */
        constexpr auto DIRECTORY_FLAGS = O_PATH | O_DIRECTORY | O_CLOEXEC;
        /** Flags for send() and sendmsg(): a peer that went away returns EPIPE instead of raising SIGPIPE. */
        constexpr auto SEND_FLAGS = MSG_NOSIGNAL;
        /** Flags for recvmsg(): received descriptors are closed on exec. */
        constexpr auto RECEIVE_FLAGS = MSG_WAITALL | MSG_CMSG_CLOEXEC;
#else
        constexpr auto DIRECTORY_FLAGS = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        constexpr auto SEND_FLAGS = 0;
        constexpr auto RECEIVE_FLAGS = MSG_WAITALL;
#endif

        /**
         * Mark a descriptor close on exec.
         * \param[in] fd The descriptor.
         */
        void setCloseOnExec(int fd) noexcept
        {
            if (fd >= 0)
            {
                ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
            }
        }

        /**
         * Create a pipe whose ends are closed on exec.
         * \param[out] out_ends Receives the read and write ends.
         * \return 0 on success, -1 on error.
         */
        int makePipe(std::array<int, 2>& out_ends) noexcept
        {
#ifdef __linux__
            return ::pipe2(out_ends.data(), O_CLOEXEC);
#else
            auto const result = ::pipe(out_ends.data());
            setCloseOnExec(out_ends[0]);
            setCloseOnExec(out_ends[1]);
            return result;
#endif
        }

        /**
         * Accept a connection whose socket is closed on exec and does not raise SIGPIPE.
         * \param[in] listener The listening socket.
         * \return The connected socket, or an empty object on error.
         */
        UniqueFd acceptClient(int listener) noexcept
        {
#ifdef __linux__
            return UniqueFd{::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC)};
#else
            auto result = UniqueFd{::accept(listener, nullptr, nullptr)};
            setCloseOnExec(result.get());
            auto const on = 1;
            ::setsockopt(result.get(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
            return result;
#endif
        }

        /**
         * \return A new Unix stream socket, closed on exec, that does not raise SIGPIPE, or an empty object on error.
         */
        UniqueFd makeSocket() noexcept
        {
#ifdef __linux__
            return UniqueFd{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
#else
            auto result = UniqueFd{::socket(AF_UNIX, SOCK_STREAM, 0)};
            setCloseOnExec(result.get());
            auto const on = 1;
            ::setsockopt(result.get(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
            return result;
#endif
        }

        /** The request a client sends after connecting. */
        struct Request
        {
            std::uint32_t magic;
            std::uint32_t version;
        };

        /** The header of each reply message. The descriptors travel in the control data. */
        struct ReplyHeader
        {
            std::uint32_t magic;
            std::uint32_t version;
            std::uint32_t total;  ///< Number of descriptors in the whole reply.
            std::uint32_t offset; ///< Index of the first descriptor of this message in the whole reply.
        };

        /**
         * A socket address for a path, and what keeps it valid.
         *
         * A Unix socket address holds at most 107 bytes of path. A longer path is reached through the directory's
         * entry in /proc/self/fd, which a directory descriptor held open by this object keeps valid.
         */
        struct SocketAddress
        {
            sockaddr_un address{};
            UniqueFd directory;
        };

        /**
         * \param[in] path The socket path.
         * \return The address to bind or connect to.
         * \throws std::system_error if a long path's directory cannot be opened.
         * \throws std::invalid_argument if even the shortened path does not fit.
         */
        SocketAddress makeAddress(std::filesystem::path const& path)
        {
            auto result = SocketAddress{};
            result.address.sun_family = AF_UNIX;

            auto text = path.string();
#ifdef __linux__
            if (text.size() >= sizeof(result.address.sun_path))
            {
                result.directory = UniqueFd{::open(path.parent_path().c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC)};
                if (result.directory.get() < 0)
                {
                    throw std::system_error{errno, std::generic_category(), "Cannot open the directory of " + text};
                }
                text = fmt::format("/proc/self/fd/{}/{}", result.directory.get(), path.filename().string());
            }
#endif
            if (text.size() >= sizeof(result.address.sun_path))
            {
                throw std::invalid_argument{"The socket path is too long: " + path.string()};
            }
            std::memcpy(result.address.sun_path, text.c_str(), text.size() + 1U);
            return result;
        }

        /**
         * Set the send and receive timeouts of a connected socket.
         * \param[in] fd The socket.
         */
        void setTimeouts(int fd) noexcept
        {
            auto timeout = timeval{};
            timeout.tv_sec = IO_TIMEOUT_SECONDS;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        }

        /** A control message buffer large enough for one message's descriptors, aligned for cmsghdr. */
        union ControlBuffer
        {
            char bytes[CMSG_SPACE(sizeof(int) * DESCRIPTORS_PER_MESSAGE)];
            cmsghdr alignment;
        };
    }

    UniqueFd::UniqueFd(int fd) noexcept
        : _fd{fd}
    {}

    UniqueFd::~UniqueFd()
    {
        if (_fd >= 0)
        {
            ::close(_fd);
        }
    }

    UniqueFd::UniqueFd(UniqueFd&& other) noexcept
        : _fd{std::exchange(other._fd, -1)}
    {}

    UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
    {
        if (this != &other)
        {
            if (_fd >= 0)
            {
                ::close(_fd);
            }
            _fd = std::exchange(other._fd, -1);
        }
        return *this;
    }

    int UniqueFd::get() const noexcept
    {
        return _fd;
    }

    int UniqueFd::release() noexcept
    {
        return std::exchange(_fd, -1);
    }

    DescriptorServer::DescriptorServer(std::filesystem::path const& socketPath, std::vector<int> descriptors,
        std::filesystem::path const& accessReference)
        : _descriptors{std::move(descriptors)}
        , _listener{makeSocket()}
        , _directory{}
        , _socketName{socketPath.filename().string()}
        , _stopRead{}
        , _stopWrite{}
        , _thread{}
    {
        // Connecting needs write permission on the socket file. Whoever may read the reference may connect.
        struct stat reference{};
        if (::stat(accessReference.c_str(), &reference) != 0)
        {
            throw std::system_error{errno, std::generic_category(), "Cannot read the permissions of " + accessReference.string()};
        }
        auto const mode = static_cast<mode_t>(S_IRUSR | S_IWUSR | (((reference.st_mode & S_IRGRP) != 0) ? (S_IRGRP | S_IWGRP) : 0) |
                                              (((reference.st_mode & S_IROTH) != 0) ? (S_IROTH | S_IWOTH) : 0));

        auto stop = std::array<int, 2>{-1, -1};
        if ((_listener.get() < 0) || (makePipe(stop) != 0))
        {
            throw std::system_error{errno, std::generic_category(), "Cannot create the payload descriptor socket."};
        }
        _stopRead = UniqueFd{stop[0]};
        _stopWrite = UniqueFd{stop[1]};

        _directory = UniqueFd{::open(socketPath.parent_path().c_str(), DIRECTORY_FLAGS)};
        if (_directory.get() < 0)
        {
            throw std::system_error{
                errno, std::generic_category(), "Cannot open the directory of the payload descriptor socket " + socketPath.string()};
        }

        auto const address = makeAddress(socketPath);
        if (::bind(_listener.get(), reinterpret_cast<sockaddr const*>(&address.address), sizeof(address.address)) != 0)
        {
            throw std::system_error{errno, std::generic_category(), "Cannot bind the payload descriptor socket " + socketPath.string()};
        }

        // The mode is set on the file. fchmod() on the socket would only change the socket's inode in the kernel.
        struct stat bound{};
        if ((::fstatat(_directory.get(), _socketName.c_str(), &bound, AT_SYMLINK_NOFOLLOW) != 0) ||
            (::fchmodat(_directory.get(), _socketName.c_str(), mode, 0) != 0) || (::listen(_listener.get(), 16) != 0))
        {
            auto const error = errno;
            ::unlinkat(_directory.get(), _socketName.c_str(), 0);
            throw std::system_error{error, std::generic_category(), "Cannot listen on the payload descriptor socket " + socketPath.string()};
        }
        _socketDevice = static_cast<std::uint64_t>(bound.st_dev);
        _socketInode = static_cast<std::uint64_t>(bound.st_ino);

        _thread = std::thread{[this]()
            {
                serve();
            }};
    }

    DescriptorServer::~DescriptorServer()
    {
        auto const one = char{1};
        if (::write(_stopWrite.get(), &one, sizeof(one)) != static_cast<ssize_t>(sizeof(one)))
        {
            MXL_ERROR("Cannot stop the payload descriptor server: {}", std::strerror(errno));
        }
        if (_thread.joinable())
        {
            _thread.join();
        }
        removeSocket();
    }

    void DescriptorServer::removeSocket() const noexcept
    {
        // The path may hold another writer's socket by now, which must stay. It may also be gone with the flow.
        struct stat current{};
        if ((::fstatat(_directory.get(), _socketName.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0) &&
            (static_cast<std::uint64_t>(current.st_dev) == _socketDevice) && (static_cast<std::uint64_t>(current.st_ino) == _socketInode))
        {
            ::unlinkat(_directory.get(), _socketName.c_str(), 0);
        }
    }

    void DescriptorServer::serve() noexcept
    {
        auto fds = std::array<pollfd, 2>{
            pollfd{.fd = _listener.get(), .events = POLLIN, .revents = 0},
            pollfd{.fd = _stopRead.get(), .events = POLLIN, .revents = 0},
        };
        while (true)
        {
            if (::poll(fds.data(), fds.size(), -1) < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                MXL_ERROR("The payload descriptor server stopped: {}", std::strerror(errno));
                return;
            }
            if ((fds[1].revents & POLLIN) != 0)
            {
                return;
            }
            if ((fds[0].revents & POLLIN) != 0)
            {
                if (auto client = acceptClient(_listener.get()); client.get() >= 0)
                {
                    serveClient(client.get());
                }
            }
        }
    }

    void DescriptorServer::serveClient(int client) const noexcept
    {
        setTimeouts(client);

        auto request = Request{};
        if ((::recv(client, &request, sizeof(request), MSG_WAITALL) != static_cast<ssize_t>(sizeof(request))) || (request.magic != CHANNEL_MAGIC) ||
            (request.version != CHANNEL_VERSION))
        {
            MXL_WARN("Ignoring an invalid request on the payload descriptor socket.");
            return;
        }

        auto offset = std::size_t{0};
        do
        {
            auto const count = std::min(DESCRIPTORS_PER_MESSAGE, _descriptors.size() - offset);
            auto header = ReplyHeader{
                .magic = CHANNEL_MAGIC,
                .version = CHANNEL_VERSION,
                .total = static_cast<std::uint32_t>(_descriptors.size()),
                .offset = static_cast<std::uint32_t>(offset),
            };
            auto data = iovec{.iov_base = &header, .iov_len = sizeof(header)};
            auto control = ControlBuffer{};
            auto message = msghdr{};
            message.msg_iov = &data;
            message.msg_iovlen = 1;
            if (count > 0U)
            {
                message.msg_control = control.bytes;
                message.msg_controllen = CMSG_SPACE(sizeof(int) * count);
                auto* const cmsg = CMSG_FIRSTHDR(&message);
                cmsg->cmsg_level = SOL_SOCKET;
                cmsg->cmsg_type = SCM_RIGHTS;
                cmsg->cmsg_len = CMSG_LEN(sizeof(int) * count);
                std::memcpy(CMSG_DATA(cmsg), _descriptors.data() + offset, sizeof(int) * count);
            }
            if (::sendmsg(client, &message, SEND_FLAGS) != static_cast<ssize_t>(sizeof(header)))
            {
                MXL_WARN("Cannot send payload descriptors to a reader: {}", std::strerror(errno));
                return;
            }
            offset += count;
        }
        while (offset < _descriptors.size());
    }

    std::vector<UniqueFd> receiveDescriptors(std::filesystem::path const& socketPath)
    {
        auto const socket = makeSocket();
        if (socket.get() < 0)
        {
            throw std::system_error{errno, std::generic_category(), "Cannot create a socket to receive payload descriptors."};
        }
        auto const address = makeAddress(socketPath);
        if (::connect(socket.get(), reinterpret_cast<sockaddr const*>(&address.address), sizeof(address.address)) != 0)
        {
            throw std::system_error{errno, std::generic_category(), "Cannot connect to the payload descriptor socket " + socketPath.string()};
        }
        setTimeouts(socket.get());

        auto const request = Request{.magic = CHANNEL_MAGIC, .version = CHANNEL_VERSION};
        if (::send(socket.get(), &request, sizeof(request), SEND_FLAGS) != static_cast<ssize_t>(sizeof(request)))
        {
            throw std::system_error{errno, std::generic_category(), "Cannot request payload descriptors."};
        }

        auto result = std::vector<UniqueFd>{};
        auto total = std::size_t{0};
        do
        {
            auto header = ReplyHeader{};
            auto data = iovec{.iov_base = &header, .iov_len = sizeof(header)};
            auto control = ControlBuffer{};
            auto message = msghdr{};
            message.msg_iov = &data;
            message.msg_iovlen = 1;
            message.msg_control = control.bytes;
            message.msg_controllen = sizeof(control.bytes);

            auto const received = ::recvmsg(socket.get(), &message, RECEIVE_FLAGS);
            if (received < 0)
            {
                throw std::system_error{errno, std::generic_category(), "Cannot receive payload descriptors."};
            }

            // Take ownership of every descriptor that arrived before checking anything, so that none leaks.
            auto const firstNew = result.size();
            for (auto* cmsg = CMSG_FIRSTHDR(&message); cmsg != nullptr; cmsg = CMSG_NXTHDR(&message, cmsg))
            {
                if ((cmsg->cmsg_level == SOL_SOCKET) && (cmsg->cmsg_type == SCM_RIGHTS))
                {
                    auto const count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                    for (auto i = std::size_t{0}; i < count; ++i)
                    {
                        int fd = -1;
                        std::memcpy(&fd, CMSG_DATA(cmsg) + (i * sizeof(int)), sizeof(int));
                        setCloseOnExec(fd);
                        result.emplace_back(fd);
                    }
                }
            }

            if ((received != static_cast<ssize_t>(sizeof(header))) || ((message.msg_flags & MSG_CTRUNC) != 0) || (header.magic != CHANNEL_MAGIC) ||
                (header.version != CHANNEL_VERSION) || (header.offset != firstNew) || ((firstNew > 0U) && (header.total != total)) ||
                (result.size() > header.total))
            {
                throw std::runtime_error{"Received an invalid reply on the payload descriptor socket " + socketPath.string()};
            }
            total = header.total;
        }
        while (result.size() < total);

        return result;
    }
}
