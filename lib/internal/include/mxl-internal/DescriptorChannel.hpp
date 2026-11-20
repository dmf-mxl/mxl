// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <mxl/platform.h>

namespace mxl::lib
{
    /** Name of the Unix socket in a flow directory on which the writer serves the descriptors of the payload. */
    constexpr auto PAYLOAD_SOCKET_FILE_NAME = "payload.sock";

    /**
     * Owns a file descriptor and closes it on destruction.
     */
    class MXL_EXPORT UniqueFd
    {
    public:
        /** An object that owns no descriptor. */
        UniqueFd() noexcept = default;

        /**
         * \param[in] fd The descriptor to own, or -1.
         */
        explicit UniqueFd(int fd) noexcept;

        /** Closes the descriptor. */
        ~UniqueFd();

        UniqueFd(UniqueFd&& other) noexcept;
        UniqueFd& operator=(UniqueFd&& other) noexcept;
        UniqueFd(UniqueFd const&) = delete;
        UniqueFd& operator=(UniqueFd const&) = delete;

        /** \return The descriptor, or -1. */
        [[nodiscard]]
        int get() const noexcept;

        /**
         * Give up ownership without closing.
         * \return The descriptor, or -1.
         */
        [[nodiscard]]
        int release() noexcept;

    private:
        int _fd{-1};
    };

    /**
     * Serves a fixed set of file descriptors on a Unix socket, so that other processes, including processes in other
     * containers that share the directory, can receive duplicates of them.
     *
     * A thread accepts connections and sends every client all the descriptors with SCM_RIGHTS, in several messages
     * when there are more than one message can carry. Connecting requires write permission on the socket file, so the
     * server gives write permission on it to the owner, group and others that may read a reference file, the flow's
     * data file. Whoever may read the flow may then receive its payload, and nobody else.
     *
     * The socket stays reachable when its directory is renamed, which happens when a flow is published. The server
     * removes the socket when it stops, wherever its directory was renamed to, unless the path no longer is the socket
     * it bound, for example because another writer's socket replaced it.
     */
    class MXL_EXPORT DescriptorServer
    {
    public:
        /**
         * Bind the socket and start serving.
         *
         * \param[in] socketPath Where to create the socket. Must not exist.
         * \param[in] descriptors The descriptors to serve. They are not owned and must stay open while the server
         *      exists.
         * \param[in] accessReference The file whose read permission the write permission of the socket follows.
         * \throws std::system_error if the socket cannot be created, or accessReference cannot be read.
         * \throws std::invalid_argument if socketPath is too long for a socket address.
         */
        DescriptorServer(std::filesystem::path const& socketPath, std::vector<int> descriptors, std::filesystem::path const& accessReference);

        /** Stops serving, closes the socket and removes the socket file if it still is the socket this server bound. */
        ~DescriptorServer();

        DescriptorServer(DescriptorServer const&) = delete;
        DescriptorServer(DescriptorServer&&) = delete;
        DescriptorServer& operator=(DescriptorServer const&) = delete;
        DescriptorServer& operator=(DescriptorServer&&) = delete;

    private:
        /** Accept and serve connections until stopped. */
        void serve() noexcept;

        /**
         * Send all descriptors to one client.
         * \param[in] client The connected client socket.
         */
        void serveClient(int client) const noexcept;

        /** Remove the socket file if it still is the one this server bound. */
        void removeSocket() const noexcept;

        std::vector<int> _descriptors; ///< The served descriptors, not owned.
        UniqueFd _listener;            ///< The listening socket.
        UniqueFd _directory;           ///< The directory of the socket, which follows it when the directory is renamed.
        std::string _socketName;       ///< The file name of the socket in _directory.
        std::uint64_t _socketDevice{}; ///< The device of the socket file this server bound.
        std::uint64_t _socketInode{};  ///< The inode of the socket file this server bound.
        UniqueFd _stopRead;            ///< Read end of a pipe whose write end stops the thread.
        UniqueFd _stopWrite;           ///< Write end of the stop pipe.
        std::thread _thread;           ///< Accepts and serves connections.
    };

    /**
     * Receive the descriptors a DescriptorServer serves.
     *
     * \param[in] socketPath The socket to connect to.
     * \return Duplicates of the served descriptors, in the order the server holds them.
     * \throws std::system_error if the socket cannot be reached, for example because no writer serves it any more.
     * \throws std::runtime_error if the server sends an invalid reply.
     * \throws std::invalid_argument if socketPath is too long for a socket address.
     */
    MXL_EXPORT
    std::vector<UniqueFd> receiveDescriptors(std::filesystem::path const& socketPath);
}
