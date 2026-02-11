// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowState.hpp
 * @brief Base shared memory structure for flow synchronization
 *
 * FlowState is the minimal header structure stored in the "data" file of every MXL flow.
 * It lives at ${domain}/${flowId}.mxl-flow/data and is mapped by both writers and readers.
 *
 * Purpose:
 * - Detect flow recreation (via inode tracking)
 * - Provide futex synchronization point (syncCounter)
 * - Serve as base for derived structures (DiscreteFlowState, ContinuousFlowState)
 *
 * Memory layout:
 *   data file (SharedMemoryInstance<FlowState>):
 *     [inode: 8 bytes]
 *     [syncCounter: 4 bytes]
 *     [derived fields...]  // DiscreteFlowState or ContinuousFlowState adds more
 *
 * Why POD (Plain Old Data)?
 * - Must be safely shared across processes
 * - No vtables, no RTTI, no destructors
 * - Initialized via placement-new in SharedMemoryInstance
 * - All fields are atomically-accessible primitive types
 *
 * Thread-safety:
 * - inode is read-only after initialization (no synchronization needed)
 * - syncCounter is accessed atomically by futex operations
 */

#pragma once

#include <cstdint>
#include <sys/types.h>

namespace mxl::lib
{
    /**
     * Internal data relevant to the current state of an active flow.
     * This data is shared among media functions for inter-process communication
     * and synchronization.
     *
     * This is the base structure; DiscreteFlowState and ContinuousFlowState
     * inherit from this and add flow-type-specific fields.
     */
    struct FlowState
    {
        /**
         * The flow data file's inode number.
         *
         * Purpose: Detect flow recreation (stale mappings).
         * When a FlowWriter creates a flow, it stores the inode here.
         * FlowReaders check if the inode matches the current file's inode.
         * If different, the flow was deleted and recreated → reader must remap.
         *
         * Why inodes work:
         * - Filesystem guarantees inode uniqueness while file exists
         * - If file is deleted and recreated, it gets a new inode
         * - Detects the ABA problem: flow deleted, same path recreated
         *
         * Set by: FlowWriter during flow creation
         * Read by: FlowReader on every access (cached, not syscall)
         */
        ino_t inode;

        /**
         * Futex synchronization counter (32-bit for futex compatibility).
         *
         * Purpose: Efficient cross-process wait/wake for new data availability.
         *
         * Writer behavior:
         *   1. Write new grain/samples to ring buffer
         *   2. Update metadata (grainCount++, sampleOffset+=N, etc.)
         *   3. syncCounter++  (atomic increment)
         *   4. wakeAll(&syncCounter)  (futex syscall)
         *
         * Reader behavior:
         *   while (!dataAvailable) {
         *       uint32_t oldSync = syncCounter;
         *       if (!waitUntilChanged(&syncCounter, oldSync, deadline)) {
         *           return TIMEOUT;
         *       }
         *   }
         *
         * Why 32-bit:
         * - Linux futex requires 32-bit aligned word
         * - Wraps around safely (readers only check for change, not value)
         * - Atomic access guaranteed by CPU on 32-bit aligned address
         *
         * Incremented by: FlowWriter on commit operations
         * Waited on by: FlowReaders via futex (see Sync.hpp)
         */
        std::uint32_t syncCounter;

        /**
         * Default constructor that value-initializes all members to zero.
         * Called by placement-new when SharedMemoryInstance creates the flow.
         */
        constexpr FlowState() noexcept;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    constexpr FlowState::FlowState() noexcept
        : inode{}          // Initialize to 0 (will be set by FlowWriter after mapping)
        , syncCounter{}    // Initialize to 0 (incremented on every commit)
    {}
}
