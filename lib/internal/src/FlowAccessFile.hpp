// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief The access file of a flow, which readers touch so that the DomainWatcher of the writer updates the last read time.
 */

#pragma once

#include <filesystem>

namespace mxl::lib
{
    /**
     * @brief The access file of a flow, opened by a reader for touching it after each successful read.
     *
     * Opening the file may fail, for example if the domain is on a read only volume. The reader still works, but the last read time of the
     * flow is not updated.
     */
    class FlowAccessFile
    {
    public:
        /**
         * @brief Open the access file.
         * @param path Path of the access file.
         */
        explicit FlowAccessFile(std::filesystem::path const& path) noexcept;

        FlowAccessFile(FlowAccessFile const&) = delete;
        FlowAccessFile& operator=(FlowAccessFile const&) = delete;

        /** @brief Close the access file. */
        ~FlowAccessFile();

        /**
         * @brief Touch the access file. Does nothing if it could not be opened.
         * @return True if the file time was updated.
         */
        bool touch() const noexcept;

    private:
        /** File descriptor of the access file, or -1 if it could not be opened. */
        int _fd;
    };
}
