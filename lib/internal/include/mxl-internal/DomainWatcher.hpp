// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file DomainWatcher.hpp
 * @brief Background monitoring of flow access for writer feedback
 *
 * DomainWatcher uses inotify (Linux) or kqueue (macOS) to monitor flow "access"
 * files for changes. When a FlowReader touches the access file (indicating it
 * read a grain), the DomainWatcher updates FlowInfo.lastRead timestamps.
 *
 * WHY THIS EXISTS:
 * - FlowWriters want to know if anyone is reading their output (for metrics/logging)
 * - Direct reader-writer communication would violate zero-copy architecture
 * - Filesystem notifications provide decoupled, scalable monitoring
 * - Updates are asynchronous (doesn't block reader or writer)
 *
 * OPERATION:
 * 1. FlowWriter registers with DomainWatcher when created
 * 2. DomainWatcher adds inotify watch on ${flowId}.mxl-flow/access file
 * 3. Background thread (processEvents) waits on inotify fd using epoll
 * 4. When FlowReader reads grain, it touches the access file (updates mtime)
 * 5. inotify fires IN_ATTRIB event (attribute change)
 * 6. DomainWatcher finds associated FlowWriter and updates FlowInfo.lastRead
 * 7. FlowWriter unregisters when destroyed, watch removed
 *
 * PLATFORM SPECIFICS:
 * - Linux: Uses inotify + epoll for scalable file monitoring
 * - macOS: Would use kqueue (TODO: not yet implemented)
 * - Windows: Would use ReadDirectoryChangesW (not yet implemented)
 *
 * THREAD SAFETY:
 * - _watchThread runs processEvents() loop
 * - _mutex protects _watches map (add/remove/lookup)
 * - FlowWriter updates are thread-safe (atomic or internally synchronized)
 *
 * PERFORMANCE:
 * - O(1) event processing (hash map lookup by watch descriptor)
 * - Coalesced events (multiple touches merged by kernel)
 * - No polling (epoll waits efficiently until events)
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <unistd.h>
#include <uuid.h>
#include <sys/inotify.h>
#include <mxl/platform.h>
#include "mxl-internal/DiscreteFlowData.hpp"

namespace mxl::lib
{

    class DiscreteFlowWriter;

    /**
     * Record of a flow being monitored by DomainWatcher.
     * Stored in _watches multimap (one watch fd can have multiple flows).
     */
    struct DomainWatcherRecord
    {
        typedef std::shared_ptr<DomainWatcherRecord> ptr;

        uuids::uuid id;              // Flow UUID
        std::string fileName;        // File being watched (e.g., "access")
        DiscreteFlowWriter* fw;      // Writer to notify (weak pointer, not owned)
        std::shared_ptr<DiscreteFlowData> flowData;  // Shared flow data (for metadata access)

        /** Equality: same flow and same writer (multiple writers can watch same flow). */
        [[nodiscard]]
        bool operator==(DomainWatcherRecord const& other) const noexcept
        {
            return (id == other.id) && (fw == other.fw);
        }
    };

    /**
     * Monitors flow access files for reader activity notifications.
     *
     * One DomainWatcher per Instance, shared across all writers in that domain.
     * Background thread processes inotify/kqueue events and updates writers.
     */
    class MXL_EXPORT DomainWatcher
    {
    public:
        typedef std::shared_ptr<DomainWatcher> ptr;

        /**
         * Constructor: initialize inotify/kqueue and start background thread.
         *
         * Steps:
         * 1. Create inotify fd (Linux) or kqueue (macOS)
         * 2. Create epoll fd (Linux) to monitor inotify fd
         * 3. Spawn _watchThread running processEvents()
         * 4. Thread blocks on epoll_wait() until events arrive
         *
         * @param in_domain MXL domain path to monitor (e.g., "/dev/shm/mxl")
         * @throws std::runtime_error if inotify/epoll/kqueue initialization fails
         */
        explicit DomainWatcher(std::filesystem::path const& in_domain);

        /**
         * Destructor: stop event loop, remove all watches, clean up resources.
         *
         * Steps:
         * 1. Set _running = false (signals thread to exit)
         * 2. Join _watchThread (wait for processEvents to finish)
         * 3. Remove all inotify watches (inotify_rm_watch)
         * 4. Close epoll fd and inotify fd
         */
        ~DomainWatcher();

        /**
         * Add a FlowWriter to monitoring.
         *
         * Creates inotify watch on ${domain}/${flowId}.mxl-flow/access file.
         * When file is touched (reader accessed grain), updates writer's FlowInfo.lastRead.
         *
         * @param writer FlowWriter to notify (weak pointer, not owned)
         * @param id Flow UUID being written
         *
         * Thread-safety: Locks _mutex during map update.
         */
        void addFlow(DiscreteFlowWriter* writer, uuids::uuid id);

        /**
         * Remove a FlowWriter from monitoring.
         *
         * If this was the last writer for the flow, removes inotify watch.
         * Safe to call even if writer not registered (no-op).
         *
         * @param writer FlowWriter to stop notifying
         * @param id Flow UUID
         *
         * Thread-safety: Locks _mutex during map update.
         */
        void removeFlow(DiscreteFlowWriter* writer, uuids::uuid id);

        /**
         * Stop the background monitoring thread.
         * Called by destructor, but can be called early if needed.
         */
        void stop()
        {
            _running = false;
        }

        /**
         * Get the number of writers registered for a specific flow.
         * Useful for diagnostics/logging.
         * @param id Flow UUID
         * @return Number of writers watching this flow (typically 1, can be > 1 for multi-writer)
         */
        [[nodiscard]]
        std::size_t count(uuids::uuid id) const noexcept;

        /**
         * Get the total number of writer registrations across all flows.
         * @return Total watch count (sum across all flows)
         */
        [[nodiscard]]
        std::size_t size() const noexcept;

    private:
        /**
         * Background thread entry point: wait for and process inotify/kqueue events.
         *
         * Linux implementation:
         * 1. epoll_wait() blocks until inotify has events (or timeout)
         * 2. read() from inotify fd to get events
         * 3. processEventBuffer() parses events and updates writers
         * 4. Loop until _running == false
         *
         * Runs in _watchThread, spawned by constructor.
         */
        void processEvents();

#ifdef __linux__
        /**
         * Process a buffer of inotify events.
         *
         * Parses inotify_event structures, looks up associated writers in _watches,
         * and updates FlowInfo.lastRead timestamps.
         *
         * @param buffer Pointer to inotify event buffer
         * @param count Number of bytes in buffer
         */
        void processEventBuffer(struct ::inotify_event const* buffer, std::size_t count);
#elif defined __APPLE__
        // TODO: kqueue implementation for macOS
#endif

        /** MXL domain path being monitored (e.g., "/dev/shm/mxl"). */
        std::filesystem::path _domain;

#ifdef __APPLE__
        int _kq;  // kqueue file descriptor (macOS)
#elif defined __linux__
        /**
         * inotify file descriptor (Linux).
         * Created by inotify_init1(), used to add watches and read events.
         */
        int _inotifyFd;

        /**
         * epoll file descriptor (Linux).
         * Monitors _inotifyFd for readability (efficient wait for events).
         */
        int _epollFd;
#endif

        /**
         * Map of inotify watch descriptors to flow records.
         *
         * Key: watch descriptor (from inotify_add_watch)
         * Value: DomainWatcherRecord (flow id, writer pointer, etc.)
         *
         * Multimap because multiple flows might share same watch fd
         * (though typically one watch per access file).
         */
        std::unordered_multimap<int, DomainWatcherRecord> _watches;

        /** Protects _watches map during add/remove/lookup. */
        mutable std::mutex _mutex;

        /**
         * Controls background thread loop.
         * Set to false in stop() or destructor to exit processEvents().
         */
        std::atomic<bool> _running;

        /**
         * Background thread running processEvents().
         * Spawned in constructor, joined in destructor.
         */
        std::thread _watchThread;
    };

} // namespace mxl::lib
