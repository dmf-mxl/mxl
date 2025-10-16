// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <uuid.h>
#include <unordered_map>

namespace mxl::lib
{

    /// Direction of the watcher
    enum class WatcherType
    {
        READER,
        WRITER
    };

    /// Entry stored in the unordered_maps
    struct DomainWatcherRecord
    {
        typedef std::shared_ptr<DomainWatcherRecord> ptr;

        /// flow id
        uuids::uuid id;
        /// file being watched
        std::string fileName;
        /// direction of the watch
        WatcherType type;
        /// use count (many readers and writers can point to the same watch/flow
        uint16_t count;

        /// records are considered equals if their IDs and Types are equals.
        bool operator==(DomainWatcherRecord const& other) const
        {
            return id == other.id && type == other.type;
        }
    };

    ///
    /// Monitors flows on disk for changes.
    ///
    /// A flow reader is looking for changes to the {mxl_domain}/{flow_id}.mxl-flow/data file
    /// An update to this flow triggers a callback that will notify a condition variable in the relevant FlowReaders.
    /// if a reader is waiting for the next grain it will be notified that the grain is ready.
    ///
    /// A flow writer is looking for changes to the {mxl_domain}/{flow_id}.mxl-flow/access file. This file is 'touched'
    /// by readers when they read a grain, which will trigger a 'FlowInfo.lastRead` update (performed by the FlowWriter
    /// since the writer is the only one mapping the flow in write-mode).
    ///
    class DomainWatcher
    {
    public:
        using Callback = std::function<void(uuids::uuid const&, WatcherType in_type)>;

        typedef std::shared_ptr<DomainWatcher> ptr;

        ///
        /// Constructor that initializes inotify and epoll/kqueue, and starts the event processing thread.
        /// \param in_domain The mxl domain path to monitor.
        /// \param in_callback Function to be called when a monitored file's attributes change.
        ///
        explicit DomainWatcher(std::filesystem::path const& in_domain, Callback in_callback);

        ///
        /// Destructor that stops the event loop, removes all watches, and closes file descriptors.
        ///
        ~DomainWatcher();

        ///
        /// Adds a file to be watched for attribute changes and deletion.
        /// \param in_flowId the id of the flow to be monitored for attrib changes
        /// \return The current use count of this watched flow + type.
        ///
        int16_t addFlow(uuids::uuid const& in_flowId, WatcherType in_type);

        ///
        /// Removes a flow from being watched.
        /// \param in_flowId the flow to remove
        /// \return The current use count of this watched flow + type.  -1 if the watch did not exist.
        ///
        int16_t removeFlow(uuids::uuid const& in_flowId, WatcherType in_type);

        ///
        /// Stops the running thread
        ///
        void stop()
        {
            _running = false;
        }

    private:
        /// Event loop that waits for inotify file change events and processes them.
        /// (invokes the callback)
        void processEvents();
        /// The monitored domain
        std::filesystem::path _domain;
        /// The callback to invoke when a file changed
        Callback _callback;

#ifdef __APPLE__
        int _kq;
#elif defined __linux__
        /// The inotify file descriptor
        int _inotifyFd;
        /// The epoll fd monitoring inotify
        int _epollFd;
#endif

        /// Map of watch descriptors to file records.  Multiple records could use the same watchfd
        std::unordered_multimap<int, DomainWatcherRecord::ptr> _watches;
        /// Prodect maps
        std::mutex _mutex;
        /// Controls the event processing thread
        std::atomic<bool> _running;
        /// Event processing thread
        std::thread _watchThread;
    };

} // namespace mxl::lib
