// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <sys/types.h>

namespace mxl::lib
{
    /**
     * Identity of a file that stays the same while the file exists: the inode number, or the NTFS file index on Windows, where ino_t is
     * only 16 bits wide.
     */
#ifdef _WIN32
    using FileIdentity = std::uint64_t;
#else
    using FileIdentity = ino_t;
#endif

    /**
     * Internal data relevant to the current state of an active flow.
     * This data is shared among media functions for inter process communication
     * and synchronization.
     */
    struct FlowState
    {
        /**
         * The flow data inode. Used to detect if the flow was recreated and
         * the current in-memory mapping of the flow has become stale as a
         * result.
         */
        FileIdentity inode;

        /**
         * 32 bit word used synchronization between a writer and multiple readers.
         * This value can be used by futexes. When a FlowWriter commits some
         * data (a grain, a slice, etc) it will increment this value and then
         * wake all FlowReaders waiting on this memory address.
         */
        std::uint32_t syncCounter;

        /**
         * Default constructor that value initializes all members.
         */
        constexpr FlowState() noexcept;
    };

    /**************************************************************************/
    /* Inline implementatiom.                                                 */
    /**************************************************************************/

    constexpr FlowState::FlowState() noexcept
        : inode{}
        , syncCounter{}
    {}
}
