// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowData.hpp
 * @brief Base class for flow shared memory management (common to readers and writers)
 *
 * FlowData is the abstract base for DiscreteFlowData and ContinuousFlowData.
 * It encapsulates the "data" file mapping (${domain}/${flowId}.mxl-flow/data)
 * and provides common accessors for Flow structure fields.
 *
 * ARCHITECTURE:
 *
 *   FlowData (this file)
 *     - Manages SharedMemoryInstance<Flow> (the "data" file)
 *     - Provides accessors: flow(), flowInfo(), flowState()
 *     - Handles advisory locking for garbage collection
 *     |
 *     +-- DiscreteFlowData (for VIDEO/DATA)
 *     |     - Manages grain files and ring buffer
 *     |     - Provides grain-specific operations
 *     |
 *     +-- ContinuousFlowData (for AUDIO)
 *           - Manages sample ring buffer
 *           - Provides sample-specific operations
 *
 * KEY RESPONSIBILITIES:
 * - Map the flow's "data" file into shared memory
 * - Provide type-safe access to Flow structure fields
 * - Manage advisory file lock (for GC coordination)
 * - Track whether this instance created the flow (for initialization)
 *
 * LIFECYCLE:
 * 1. Constructor maps "data" file (or creates it if mode == CREATE_READ_WRITE)
 * 2. If created(), derived class initializes flow-specific fields
 * 3. Accessors provide const/non-const access to shared memory
 * 4. Destructor unmaps and releases lock (via SharedMemoryInstance)
 *
 * Thread-safety:
 * - Not thread-safe (each thread should have its own FlowData)
 * - Multiple processes can access the same flow concurrently
 * - Advisory lock coordinates garbage collection only, not data access
 */

#pragma once

#include <cstddef>
#include <mxl/platform.h>
#include "Flow.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    /**
     * Base class holding shared memory resources common to all types of flows.
     *
     * This is an abstract base class; concrete implementations are:
     * - DiscreteFlowData (for VIDEO/DATA flows with grain ring buffers)
     * - ContinuousFlowData (for AUDIO flows with sample ring buffers)
     *
     * Polymorphic (has virtual destructor) to enable proper cleanup of derived classes.
     */
    class MXL_EXPORT FlowData
    {
    public:
        /**
         * Check if the flow data file is successfully mapped.
         * @return true if mapped, false if uninitialized or mapping failed
         */
        constexpr bool isValid() const noexcept;

        /**
         * Bool conversion: allows "if (flowData) { ... }" idiom.
         * Equivalent to isValid().
         */
        constexpr explicit operator bool() const noexcept;

        /**
         * Get the access mode of the mapping (READ_ONLY or READ_WRITE).
         * Writers use READ_WRITE; readers use READ_ONLY.
         * @return Access mode
         */
        constexpr AccessMode accessMode() const noexcept;

        /**
         * Check if this instance created the flow (vs. opened existing flow).
         * Used to determine whether to initialize shared memory structures.
         * @return true if created, false if opened existing
         */
        constexpr bool created() const noexcept;

        /**
         * Get the size of the mapped "data" file in bytes.
         * @return Mapped region size
         */
        constexpr std::size_t mappedSize() const noexcept;

        /**
         * Get mutable pointer to the Flow structure in shared memory.
         * @return Pointer to Flow, or nullptr if not mapped
         */
        constexpr Flow* flow() noexcept;

        /**
         * Get const pointer to the Flow structure in shared memory.
         * @return Const pointer to Flow, or nullptr if not mapped
         */
        constexpr Flow const* flow() const noexcept;

        /**
         * Get mutable pointer to the mxlFlowInfo field within Flow.
         * This is the public API structure containing flow metadata.
         * @return Pointer to mxlFlowInfo, or nullptr if not mapped
         */
        constexpr mxlFlowInfo* flowInfo() noexcept;

        /**
         * Get const pointer to the mxlFlowInfo field within Flow.
         * @return Const pointer to mxlFlowInfo, or nullptr if not mapped
         */
        constexpr mxlFlowInfo const* flowInfo() const noexcept;

        /**
         * Get mutable pointer to the FlowState field within Flow.
         * FlowState contains internal synchronization fields (inode, syncCounter).
         * @return Pointer to FlowState, or nullptr if not mapped
         */
        constexpr FlowState* flowState() noexcept;

        /**
         * Get const pointer to the FlowState field within Flow.
         * @return Const pointer to FlowState, or nullptr if not mapped
         */
        constexpr FlowState const* flowState() const noexcept;

        /**
         * Check if the advisory lock on the "data" file is exclusive.
         * Exclusive locks are held by single writers; shared locks by readers.
         * @return true if exclusive, false if shared
         * @throws std::logic_error if no lock held
         */
        bool isExclusive() const;

        /**
         * Attempt to upgrade advisory lock from shared to exclusive (non-blocking).
         * Used when a writer needs exclusive access to update flow state.
         * @return true if upgrade succeeded, false if would block
         * @throws std::runtime_error if mapping is read-only or invalid
         */
        bool makeExclusive();

        /**
         * Virtual destructor for polymorphic base class.
         * Ensures derived class destructors are called properly.
         * Unmaps shared memory and releases advisory lock (via _flow destructor).
         */
        virtual ~FlowData();

    protected:
        /**
         * Protected constructor: move-construct from an existing SharedMemoryInstance.
         * Used by derived classes when they've already created the mapping.
         * @param flowSegement Existing SharedMemoryInstance to take ownership of
         */
        constexpr explicit FlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;

        /**
         * Protected constructor: create or open the flow "data" file.
         * Used by derived classes to initialize the base mapping.
         * @param flowFilePath Path to the "data" file
         * @param mode READ_ONLY, READ_WRITE, or CREATE_READ_WRITE
         * @param lockMode Advisory lock mode (Shared, Exclusive, or None)
         * @throws std::runtime_error if mapping fails
         */
        FlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

    private:
        /**
         * Shared memory mapping of the flow's "data" file.
         * This contains the Flow structure (mxlFlowInfo + FlowState + derived fields).
         * Lifetime managed by RAII (unmapped and lock released on destruction).
         */
        SharedMemoryInstance<Flow> _flow;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    constexpr FlowData::FlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : _flow{std::move(flowSegement)}
    {}

    constexpr bool FlowData::isValid() const noexcept
    {
        return _flow.isValid();
    }

    constexpr FlowData::operator bool() const noexcept
    {
        return _flow.isValid();
    }

    constexpr AccessMode FlowData::accessMode() const noexcept
    {
        return _flow.accessMode();
    }

    constexpr bool FlowData::created() const noexcept
    {
        return _flow.created();
    }

    constexpr std::size_t FlowData::mappedSize() const noexcept
    {
        return _flow.mappedSize();
    }

    constexpr Flow* FlowData::flow() noexcept
    {
        return _flow.get();
    }

    constexpr Flow const* FlowData::flow() const noexcept
    {
        return _flow.get();
    }

    constexpr mxlFlowInfo* FlowData::flowInfo() noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->info;
        }
        return nullptr;
    }

    constexpr mxlFlowInfo const* FlowData::flowInfo() const noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->info;
        }
        return nullptr;
    }

    constexpr FlowState* FlowData::flowState() noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->state;
        }
        return nullptr;
    }

    constexpr FlowState const* FlowData::flowState() const noexcept
    {
        if (auto const flow = _flow.get(); flow != nullptr)
        {
            return &flow->state;
        }
        return nullptr;
    }
}
