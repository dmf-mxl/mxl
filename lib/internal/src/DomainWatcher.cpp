// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/DomainWatcher.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include <mxl/platform.h>
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

#ifdef __APPLE__
#   include <sys/event.h>
#elif __linux__
#   include <sys/epoll.h>
#   include <sys/inotify.h>
#endif

namespace mxl::lib
{
    DomainWatcher::DomainWatcher(std::filesystem::path const& in_domain, Callback in_callback)
        : _domain{in_domain}
        , _callback{std::move(in_callback)}
        , _running{true}
    {
        // Validate that the domain path is a directory
        if (!std::filesystem::is_directory(in_domain))
        {
            throw std::filesystem::filesystem_error("Domain path is not a directory", in_domain, std::make_error_code(std::errc::not_a_directory));
        }
#ifdef __APPLE__
        /* Open a kernel queue. */
        if ((_kq = kqueue()) < 0)
        {
            auto const error = errno;
            throw std::system_error(error, std::generic_category(), "Failed to create a kqueue");
        }
#elif defined __linux__
        _inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (_inotifyFd == -1)
        {
            auto const error = errno;
            MXL_ERROR("inotify_init1 failed: {}", std::strerror(error));
            throw std::system_error(error, std::generic_category(), "inotify_init1 failed");
        }

        // Create epoll instance (close-on-exec), so child processes do not inherit it.
        // This is necessary to avoid file descriptor leaks in child processes.
        _epollFd = epoll_create1(EPOLL_CLOEXEC);
        if (_epollFd == -1)
        {
            auto const error = errno;
            MXL_ERROR("epoll_create1 failed (errno {}: {})", error, std::strerror(error));
            // Clean up inotify FD before throwing
            ::close(_inotifyFd);
            throw std::system_error(error, std::generic_category(), "epoll_create1 failed");
        }

        // Add inotify file descriptor to epoll
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = _inotifyFd;
        if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, _inotifyFd, &event) == -1)
        {
            auto const error = errno;
            MXL_ERROR("epoll_ctl ADD(inotify) failed: {}", std::strerror(error));
            ::close(_inotifyFd);
            ::close(_epollFd);
            throw std::system_error(error, std::generic_category(), "epoll_ctl ADD inotify failed");
        }
#endif

        // Start event processing thread
        try
        {
            _watchThread = std::thread(&DomainWatcher::processEvents, this);
        }
        catch (std::exception const& e)
        {
            MXL_ERROR("Failed to start watcher thread: {}", e.what());
#ifdef __APPLE__
            ::close(_kq);
#elif defined __linux__
            ::close(_inotifyFd);
            ::close(_epollFd);
#endif
            throw;
        }
        catch (...)
        {
            MXL_ERROR("Failed to start watcher thread: unknown exception");
#ifdef __APPLE__
            ::close(_kq);
#elif defined __linux__
            ::close(_inotifyFd);
            ::close(_epollFd);
#endif
            throw;
        }
    }

    DomainWatcher::~DomainWatcher()
    {
        _running = false;
        if (_watchThread.joinable())
        {
            _watchThread.join();
        }

#ifdef __APPLE__
        ::close(_kq);
#elif defined __linux__
        // Linux: remove all inotify watches and close FDs
        for (auto const& [wd, rec] : _watches)
        {
            if (inotify_rm_watch(_inotifyFd, wd) == -1)
            {
                auto const error = errno;
                MXL_ERROR("Error removing inotify watch (wd={}): {}", wd, std::strerror(error));
            }
        }
        if (::close(_inotifyFd) == -1)
        {
            auto const error = errno;
            MXL_ERROR("Error closing inotify FD: {}", std::strerror(error));
        }
        if (::close(_epollFd) == -1)
        {
            auto const error = errno;
            MXL_ERROR("Error closing epoll FD: {}", std::strerror(error));
        }
#endif
    }

    int16_t DomainWatcher::addFlow(uuids::uuid const& in_flowId, WatcherType in_type)
    {
        auto id = uuids::to_string(in_flowId);
        MXL_DEBUG("Add Flow : {}", id);

        // Create a record for this watched file
        auto record = std::make_shared<DomainWatcherRecord>();
        record->count = 0;
        record->id = in_flowId;
        record->type = in_type;

        auto const flowDirectory = makeFlowDirectoryName(_domain, id);
        record->fileName = (in_type == WatcherType::READER) ? makeFlowDataFilePath(flowDirectory) : makeFlowAccessFilePath(flowDirectory);

        // try to find it in the maps.
        std::lock_guard<std::mutex> lock(_mutex);
        int16_t useCount = 0;
        bool found = false;

        // Check if this flow is already being watched
        for (auto const& [wd, rec] : _watches)
        {
            if (*rec == *record)
            {
                found = true;
                rec->count++;
                useCount = rec->count;
            }
        }

        if (!found)
        {
            MXL_DEBUG("Record for {} not found, creating one.", id);
            record->count = 1;

#ifdef __APPLE__
            int wd = open(record->fileName.c_str(), O_EVTONLY);
#elif defined __linux__
            // if not found, add the watch and add it to the maps
            int wd = inotify_add_watch(_inotifyFd, record->fileName.c_str(), ((in_type == WatcherType::READER) ? IN_MODIFY : IN_ACCESS) | IN_ATTRIB);
#endif
            if (wd == -1)
            {
                auto const error = errno;
                MXL_ERROR("Failed to add watch for file '{}': {}", record->fileName, std::strerror(error));
                throw std::system_error(error, std::generic_category(), "Failed to add watch for file: " + record->fileName);
            }
            else
            {
                MXL_DEBUG("Added watch {} for file: {}", wd, record->fileName);
            }
            _watches.emplace(wd, record);
            useCount = record->count;
        }
        return useCount;
    }

    int16_t DomainWatcher::removeFlow(uuids::uuid const& in_flowId, WatcherType in_type)
    {
        auto id = uuids::to_string(in_flowId);

        // Create a record for this watched file
        auto record = std::make_shared<DomainWatcherRecord>();
        record->count = 0;
        record->id = in_flowId;
        record->type = in_type;
        record->fileName = _domain / id;

        int16_t useCount = -1;
        std::lock_guard<std::mutex> lock(_mutex);

        // Find the watch record that matches this flow
        for (auto const& [wd, rec] : _watches)
        {
            if (*rec == *record)
            {
                if (rec->count == 1)
                {
#ifdef __APPLE__
                    if (::close(wd) == -1)
                    {
                        MXL_ERROR("Error closing file descriptor {} for '{}'", wd, rec->fileName);
                    }
#elif defined __linux__
                    auto f = std::filesystem::path{rec->fileName};
                    // Try to remove the watch only if the file still exists.
                    if (exists(f) && (inotify_rm_watch(_inotifyFd, wd) == -1))
                    {
                        auto const error = errno;
                        MXL_ERROR("Failed to remove inotify watch (wd={}) for '{}': {}", wd, rec->fileName, std::strerror(error));
                        // Continue with cleanup despite failure
                    }
#endif
                    rec->count--;
                    useCount = rec->count;

                    // Find all elements for that watch descriptor and remove the one that matches the record.
                    auto range = _watches.equal_range(wd);
                    for (auto it = range.first; it != range.second; ++it)
                    {
                        if (*it->second == *record)
                        {
                            _watches.erase(it);
                            break;
                        }
                    }
                }
                else if (rec->count == 0)
                {
                    MXL_DEBUG("Should not have 0 use-count record");
                    useCount = -1;
                }
                else
                {
                    rec->count--;
                    useCount = rec->count;
                }
                break;
            }
        }

        return useCount;
    }

    void DomainWatcher::processEvents()
    {
#ifdef __APPLE__

        /* Set up a list of events to monitor. */
        constexpr unsigned int vnodeEvents = NOTE_DELETE | NOTE_WRITE | NOTE_ATTRIB;
        std::vector<struct kevent> eventsToMonitor, eventData;
        std::vector<DomainWatcherRecord::ptr> eventsToMonitorRecords;

        while (_running)
        {
            size_t watchCount = 0;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                watchCount = _watches.size();
                eventsToMonitor.reserve(watchCount);
                eventsToMonitor.resize(watchCount);
                eventsToMonitorRecords.reserve(watchCount);
                size_t index = 0;
                for (auto const& [wd, rec] : _watches)
                {
                    // keep the record alive.  this ensures that it is not released whilst waiting for an event.
                    eventsToMonitorRecords.push_back(rec);
                    EV_SET(&eventsToMonitor[index], wd, EVFILT_VNODE, EV_ADD | EV_CLEAR, vnodeEvents, 0, rec.get());
                    index++;
                }
            }

            timespec timeout;
            timeout.tv_sec = 0;          // 0 seconds
            timeout.tv_nsec = 250000000; // 250 milliseconds

            eventData.resize(watchCount);
            int eventCount = kevent(_kq, eventsToMonitor.data(), watchCount, eventData.data(), watchCount, &timeout);
            if (eventCount < 0)
            {
                auto const error = errno;
                MXL_ERROR("kevent error {}", std::strerror(error));
                break;
            }
            else if (eventCount)
            {
                for (int eventIndex = 0; eventIndex < eventCount; eventIndex++)
                {
                    DomainWatcherRecord* rec = (DomainWatcherRecord*)eventData[eventIndex].udata;
                    _callback(rec->id, rec->type);
                }
            }
        }

#elif defined __linux__
        epoll_event events[1];
        alignas(alignof(struct inotify_event)) char buffer[4096] = {};

        while (_running)
        {
            int nfds = epoll_wait(_epollFd, events, 1, 250);
            if (nfds == -1)
            {
                auto const error = errno;
                if (error == EINTR)
                {
                    continue; // interrupted by signal, retry loop
                }
                MXL_ERROR("epoll_wait failed: {}", std::strerror(error));
                break; // exit thread on critical epoll error
            }
            if (nfds == 0)
            {
                continue; // no events, continue looping
            }

            // We have an inotify event ready
            ssize_t length = read(_inotifyFd, buffer, sizeof(buffer));
            if (length == -1)
            {
                auto const error = errno;
                if ((error == EINTR) || (error == EAGAIN))
                {
                    continue; // spurious interrupt or non-blocking empty read, retry
                }
                MXL_ERROR("Error reading inotify events: {}", std::strerror(error));
                break; // break on critical read error
            }
            if (length == 0)
            {
                continue; // nothing to read (should not happen if nfds > 0)
            }

            std::lock_guard<std::mutex> lock(_mutex);
            for (char* ptr = buffer; ptr < buffer + length;)
            {
                struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);

                auto it = _watches.find(event->wd);
                if (it != _watches.end())
                {
                    if ((event->mask & (IN_ACCESS | IN_MODIFY | IN_ATTRIB)) != 0)
                    {
                        try
                        {
                            _callback(it->second->id, it->second->type);
                        }
                        catch (std::exception const& e)
                        {
                            MXL_ERROR("Exception in DomainWatcher callback: {}", e.what());
                        }
                        catch (...)
                        {
                            MXL_ERROR("Unknown exception in DomainWatcher callback");
                        }
                    }
                }
                ptr += sizeof(struct inotify_event) + event->len;
            }
        }
#endif
    }

} // namespace mxl::lib
