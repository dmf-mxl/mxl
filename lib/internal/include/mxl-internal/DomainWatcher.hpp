// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

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

    /// Entry stored in the unordered_maps
    struct DomainWatcherRecord
    {
        typedef std::shared_ptr<DomainWatcherRecord> ptr;

        /// flow id
        uuids::uuid id;
        /// file being watched
        std::string fileName;

        DiscreteFlowWriter* fw;

        std::shared_ptr<DiscreteFlowData> flowData;

        [[nodiscard]]
        bool operator==(DomainWatcherRecord const& other) const noexcept
        {
            return (id == other.id) && (fw == other.fw);
        }
    };

    ///
    /// Monitors flows on disk for changes.
    ///
    /// A flow writer is looking for changes to the {mxl_domain}/{flow_id}.mxl-flow/access file. This file is 'touched'
    /// by readers when they read a grain, which will trigger a 'FlowInfo.lastRead` update.
    ///
    class MXL_EXPORT DomainWatcher
    {
    public:
        typedef std::shared_ptr<DomainWatcher> ptr;

        ///
        /// Constructor that initializes inotify and epoll/kqueue, and starts the event processing thread.
        /// \param in_domain The mxl domain path to monitor.
        /// \param in_callback Function to be called when a monitored file's attributes change.
        ///
        explicit DomainWatcher(std::filesystem::path const& in_domain);

        ///
        /// Destructor that stops the event loop, removes all watches, and closes file descriptors.
        ///
        ~DomainWatcher();

        ///
        /// Add a new FlowWriter reference to the DomainWatcher.
        /// \param writer The FlowWriter reference
        /// \param id Id of the flow the FlowWriter is writing to.
        ///
        void addFlow(DiscreteFlowWriter* writer, uuids::uuid id);

        ///
        /// Remove a FlowWriter reference from the DomainWatcher.
        /// If no more FlowWriter references for a given flow id are present in the DomainWatcher
        /// it stops watching the flow.
        /// \param writer The flow writer reference to remove.
        /// \param id Id of the flow the FlowWriter is writing to.
        void removeFlow(DiscreteFlowWriter* writer, uuids::uuid id);

        ///
        /// Stops the running thread
        ///
        void stop()
        {
            _running = false;
        }

        /** \brief Returns the number of writers registered for flow id 'id'
         */
        [[nodiscard]]
        std::size_t count(uuids::uuid id) const noexcept;

        /** \brief Returns the total number of writers that are registered in the DomainWatcher
         */
        [[nodiscard]]
        std::size_t size() const noexcept;

    private:
        /// Event loop that waits for inotify file change events and processes them.
        /// (invokes the callback)
        void processEvents();

#ifdef __linux__
        void processEventBuffer(struct ::inotify_event const* buffer, std::size_t count);
#elif defined __APPLE__
        // TODO
#endif

        /// The monitored domain
        std::filesystem::path _domain;

#ifdef __APPLE__
        int _kq;
#elif defined __linux__
        /// The inotify file descriptor
        int _inotifyFd;
        /// The epoll fd monitoring inotify
        int _epollFd;
#endif

        /// Map of watch descriptors to file records.  Multiple records could use the same watchfd
        std::unordered_multimap<int, DomainWatcherRecord> _watches;
        /// Prodect maps
        mutable std::mutex _mutex;
        /// Controls the event processing thread
        std::atomic<bool> _running;
        /// Event processing thread
        std::thread _watchThread;
    };

} // namespace mxl::lib
