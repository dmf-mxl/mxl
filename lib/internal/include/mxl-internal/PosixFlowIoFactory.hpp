// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file PosixFlowIoFactory.hpp
 * @brief Concrete factory implementation for POSIX systems (Linux, macOS)
 *
 * PosixFlowIoFactory is the concrete implementation of FlowIoFactory for POSIX-compliant systems.
 * It creates readers/writers that use:
 * - mmap() for shared memory
 * - inotify/kqueue for file monitoring
 * - futex for cross-process synchronization
 *
 * This is the default (and currently only) factory implementation.
 * Future implementations might include:
 * - Windows: Using CreateFileMapping, WaitForSingleObject, etc.
 * - Embedded: Using specialized DMA engines or hardware buffers
 */

#pragma once

#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/FlowIoFactory.hpp"

namespace mxl::lib
{
    /**
     * POSIX implementation of FlowIoFactory.
     * Creates readers/writers using POSIX APIs (mmap, inotify, futex).
     */
    struct MXL_EXPORT PosixFlowIoFactory : FlowIoFactory
    {
        /**
         * Constructor: takes DomainWatcher for writer registration.
         * Writers created by this factory will register with the watcher
         * to receive reader activity notifications.
         *
         * @param watcher Shared DomainWatcher instance (one per domain)
         */
        PosixFlowIoFactory(DomainWatcher::ptr watcher);

        /** Create discrete flow reader (POSIX implementation). */
        virtual std::unique_ptr<DiscreteFlowReader> createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const override;

        /** Create continuous flow reader (POSIX implementation). */
        virtual std::unique_ptr<ContinuousFlowReader> createContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const override;

        /** Create discrete flow writer (POSIX implementation). */
        virtual std::unique_ptr<DiscreteFlowWriter> createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const override;

        /** Create continuous flow writer (POSIX implementation). */
        virtual std::unique_ptr<ContinuousFlowWriter> createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const override;

        /** Destructor. */
        ~PosixFlowIoFactory();

    private:
        /**
         * Shared DomainWatcher for this factory.
         * Passed to all created writers for reader activity tracking.
         */
        DomainWatcher::ptr _watcher;
    };
}
