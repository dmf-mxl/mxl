// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PosixFlowIoFactory.cpp
 * @brief Concrete factory for POSIX shared-memory based readers and writers
 *
 * PosixFlowIoFactory is the production implementation of FlowIoFactory that creates
 * readers and writers using POSIX shared memory primitives (mmap, futex).
 *
 * This factory creates:
 * - PosixDiscreteFlowReader: Reads video/data grains from mmap'd ring buffer files
 * - PosixDiscreteFlowWriter: Writes video/data grains, integrates with DomainWatcher
 * - PosixContinuousFlowReader: Reads audio samples from mmap'd per-channel buffers
 * - PosixContinuousFlowWriter: Writes audio samples
 *
 * Key implementation details:
 * - Uses tmpfs for zero-copy shared memory (no kernel copy)
 * - Memory mappings use PROT_READ for readers (safe, can't corrupt)
 * - Memory mappings use PROT_READ|PROT_WRITE for writers
 * - Advisory locks (flock) for garbage collection coordination
 * - Futexes for efficient reader/writer synchronization
 *
 * The DomainWatcher integration:
 * - Only discrete writers need the watcher (to track read access times)
 * - The watcher monitors file access to update lastReadTime
 * - This enables intelligent garbage collection decisions
 */

#include "mxl-internal/PosixFlowIoFactory.hpp"
#include "PosixContinuousFlowReader.hpp"
#include "PosixContinuousFlowWriter.hpp"
#include "PosixDiscreteFlowReader.hpp"
#include "PosixDiscreteFlowWriter.hpp"

namespace mxl::lib
{
    /**
     * @brief Construct a POSIX flow factory with domain watcher
     *
     * @param watcher Shared pointer to the domain watcher for tracking flow access
     *                (required for discrete writers, not used by readers/continuous writers)
     */
    PosixFlowIoFactory::PosixFlowIoFactory(DomainWatcher::ptr watcher)
        : _watcher{std::move(watcher)}
    {}

    /**
     * @brief Destructor
     */
    PosixFlowIoFactory::~PosixFlowIoFactory() = default;

    /**
     * @brief Create a POSIX-based discrete flow reader
     *
     * Creates a reader that accesses video/data grains via read-only memory mappings.
     * Each grain is in its own mmap'd file for efficient random access.
     *
     * @param manager Flow manager for domain context
     * @param flowId UUID of the flow
     * @param data Discrete flow metadata (ownership transferred)
     * @return Unique pointer to the created reader
     */
    std::unique_ptr<DiscreteFlowReader> PosixFlowIoFactory::createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<DiscreteFlowData>&& data) const
    {
        return std::make_unique<PosixDiscreteFlowReader>(manager, flowId, std::move(data));
    }

    /**
     * @brief Create a POSIX-based continuous flow reader
     *
     * Creates a reader that accesses audio samples via read-only memory mappings.
     * All channels are in a single mmap'd file with strided layout.
     *
     * @param manager Flow manager for domain context
     * @param flowId UUID of the flow
     * @param data Continuous flow metadata (ownership transferred)
     * @return Unique pointer to the created reader
     */
    std::unique_ptr<ContinuousFlowReader> PosixFlowIoFactory::createContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data) const
    {
        return std::make_unique<PosixContinuousFlowReader>(manager, flowId, std::move(data));
    }

    /**
     * @brief Create a POSIX-based discrete flow writer
     *
     * Creates a writer that writes video/data grains via read-write memory mappings.
     * The writer registers with the DomainWatcher to enable read-time tracking.
     *
     * @param manager Flow manager for domain context
     * @param flowId UUID of the flow
     * @param data Discrete flow metadata (ownership transferred)
     * @return Unique pointer to the created writer
     *
     * @note The watcher parameter is passed to the writer for registration
     */
    std::unique_ptr<DiscreteFlowWriter> PosixFlowIoFactory::createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<DiscreteFlowData>&& data) const
    {
        return std::make_unique<PosixDiscreteFlowWriter>(manager, flowId, std::move(data), _watcher);
    }

    /**
     * @brief Create a POSIX-based continuous flow writer
     *
     * Creates a writer that writes audio samples via read-write memory mappings.
     * All channels are written to a single mmap'd file with strided layout.
     *
     * @param manager Flow manager for domain context
     * @param flowId UUID of the flow
     * @param data Continuous flow metadata (ownership transferred)
     * @return Unique pointer to the created writer
     */
    std::unique_ptr<ContinuousFlowWriter> PosixFlowIoFactory::createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
        std::unique_ptr<ContinuousFlowData>&& data) const
    {
        return std::make_unique<PosixContinuousFlowWriter>(manager, flowId, std::move(data));
    }
}
