// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowWriter.hpp
 * @brief Abstract base class for writing media to MXL flows
 *
 * FlowWriter is the interface for producing media into shared memory flows.
 * It's specialized by:
 * - DiscreteFlowWriter (for VIDEO/DATA): writes individual grains
 * - ContinuousFlowWriter (for AUDIO): writes sample buffers
 *
 * ARCHITECTURE:
 *
 *   FlowWriter (this file - abstract base)
 *     - Provides common metadata access (flowInfo, configInfo, runtimeInfo)
 *     - Manages exclusive vs. shared writer mode
 *     - Tracks flow ID
 *     |
 *     +-- DiscreteFlowWriter
 *     |     - Grain API: getGrain(), commitGrain(), commitSlices()
 *     |     - Allocates and manages grain files
 *     |     - Updates grain ring buffer metadata
 *     |     - Wakes readers via syncCounter futex
 *     |
 *     +-- ContinuousFlowWriter
 *           - Samples API: writeSamples(), commitSamples()
 *           - Manages "channels" sample ring buffer
 *           - Updates sample positions and counts
 *           - Wakes readers via syncCounter futex
 *
 * KEY RESPONSIBILITIES:
 * - Create or open flow (create "data" file, grain files, or sample buffer)
 * - Provide zero-copy access for writing media
 * - Update flow metadata atomically (grain counts, sample positions)
 * - Signal readers using futex after committing data
 * - Manage advisory locks (exclusive for single writer, shared for multi-writer)
 * - Periodically touch files to prevent garbage collection
 *
 * EXCLUSIVE VS. SHARED WRITERS:
 * - Exclusive: Only one writer, holds exclusive advisory lock (typical case)
 * - Shared: Multiple writers, each holds shared advisory lock (rare, for distributed sources)
 * - Writers can attempt to upgrade from shared to exclusive via makeExclusive()
 * - Advisory locks prevent garbage collection of active flows, not data synchronization
 *
 * USAGE PATTERN:
 * 1. Instance::createFlowWriter(flowDef, options) creates writer
 * 2. Writer creates flow directory, "data" file, and grain/sample storage
 * 3. Application calls getGrain() or writeSamples() to produce media
 * 4. Writer updates metadata (grainCount++, sampleOffset+=N)
 * 5. Writer calls wakeAll(&syncCounter) to notify readers
 * 6. Application calls Instance::releaseWriter() when done
 *
 * Thread-safety:
 * - Each thread must have its own FlowWriter instance
 * - Multiple writers on the same flow possible (shared lock mode)
 * - Application responsible for coordinating writes if multiple writers
 *
 * Lifecycle:
 * - Writer holds advisory lock for its lifetime
 * - Periodically touches files to update mtime (GC prevention)
 * - Destructor releases lock (allows GC if no other references)
 */

#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/FlowData.hpp"

namespace mxl::lib
{
    /**
     * Abstract base class for flow writers (VIDEO/AUDIO/DATA producers).
     *
     * Concrete implementations:
     * - DiscreteFlowWriter (grains for VIDEO/DATA)
     * - ContinuousFlowWriter (samples for AUDIO)
     *
     * Polymorphic (has virtual destructor and pure virtual methods).
     */
    class MXL_EXPORT FlowWriter
    {
    public:
        /**
         * Get the UUID of the flow being written.
         * @return Flow UUID (immutable after construction)
         */
        [[nodiscard]]
        uuids::uuid const& getId() const;

        /**
         * Get the underlying FlowData (shared memory mapping).
         *
         * Provides access to Flow structure in "data" file.
         * Writer must have successfully created/opened the flow first.
         *
         * @return Const reference to FlowData
         * @throws May throw if flow not created/opened (implementation-defined)
         */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const = 0;

        /**
         * Get a copy of the current flow metadata.
         *
         * Returns: Ring buffer params, format, dimensions, grain rate, etc.
         * Safe to cache (returns copy, not reference to shared memory).
         *
         * Writer must have successfully created/opened the flow first.
         *
         * @return Copy of mxlFlowInfo structure
         * @throws May throw if flow not created/opened (implementation-defined)
         */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const = 0;

        /**
         * Get a copy of the immutable flow configuration metadata.
         *
         * Returns: Flow creation parameters, buffer sizes, format details.
         * This is the subset of flowInfo that never changes after creation.
         *
         * Writer must have successfully created/opened the flow first.
         *
         * @return Copy of mxlFlowConfigInfo structure
         * @throws May throw if flow not created/opened (implementation-defined)
         */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const = 0;

        /**
         * Get a copy of the current runtime state metadata.
         *
         * Returns: Current grain count, sample positions, writer activity, etc.
         * This is the subset of flowInfo that changes during operation.
         * Snapshot may be stale immediately after return (volatile data).
         *
         * Writer must have successfully created/opened the flow first.
         *
         * @return Copy of mxlFlowRuntimeInfo structure
         * @throws May throw if flow not created/opened (implementation-defined)
         */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const = 0;

        /**
         * Check if this writer has an exclusive lock (vs. shared lock).
         *
         * Exclusive lock: Only this writer can write to the flow
         * Shared lock: Multiple writers may write concurrently (app must coordinate)
         *
         * This queries the advisory file lock on the "data" file, used for
         * garbage collection coordination, not data synchronization.
         *
         * @return true if exclusive, false if shared
         * @throws May throw if no lock held (implementation-defined)
         */
        virtual bool isExclusive() const = 0;

        /**
         * Attempt to upgrade lock from shared to exclusive (non-blocking).
         *
         * Use case: Writer initially creates flow with shared lock, then wants
         * to ensure it's the only writer before proceeding.
         *
         * @return true if upgrade succeeded (now exclusive writer),
         *         false if upgrade failed (another writer still exists)
         * @throws May throw if lock upgrade operation fails (implementation-defined)
         */
        virtual bool makeExclusive() = 0;

        /**
         * Virtual destructor for polymorphic base class.
         *
         * Derived classes:
         * - Unmap shared memory regions
         * - Release advisory locks
         * - Clean up resources
         *
         * Advisory lock release allows garbage collection if no other references.
         */
        virtual ~FlowWriter();

    protected:
        /**
         * Protected constructor: move flow ID.
         * Used by derived classes during construction.
         * @param flowId Flow UUID (moved)
         */
        explicit FlowWriter(uuids::uuid&& flowId);

        /**
         * Protected constructor: copy flow ID.
         * Used by derived classes during construction.
         * @param flowId Flow UUID (copied)
         */
        explicit FlowWriter(uuids::uuid const& flowId);

    private:
        /**
         * UUID of the flow being written.
         * Used to construct flow directory path (${domain}/${flowId}.mxl-flow).
         * Immutable after construction.
         */
        uuids::uuid _flowId;
    };
}
