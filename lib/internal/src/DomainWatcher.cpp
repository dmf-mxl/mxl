// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/DomainWatcher.hpp"
#include <cstdint>
#include <cstring>
#include <algorithm>
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
#include "mxl-internal/DiscreteFlowWriter.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/Timing.hpp"

#ifdef __APPLE__
#   include <sys/event.h>
#elif __linux__
#   include <sys/epoll.h>
#   include <sys/inotify.h>
#endif

namespace mxl::lib
{
    DomainWatcher::DomainWatcher(std::filesystem::path const& in_domain)
        : _domain{in_domain}
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

    std::size_t DomainWatcher::count(uuids::uuid id) const noexcept
    {
        auto lock = std::lock_guard{_mutex};
        auto it = std::ranges::find_if(_watches, [id](auto const& item) { return item.second.id == id; });
        if (it == _watches.end())
        {
            return 0;
        }

        return _watches.count(it->first);
    }

    std::size_t DomainWatcher::size() const noexcept
    {
        auto lock = std::lock_guard{_mutex};
        return _watches.size();
    }

    void DomainWatcher::addFlow(DiscreteFlowWriter* writer, uuids::uuid id)
    {
        auto record = DomainWatcherRecord{
            .id = id,
            .fileName = makeFlowAccessFilePath(makeFlowDirectoryName(_domain, uuids::to_string(id))),
            .fw = writer,
            .flowData = {},
        };
        {
            auto lock = std::lock_guard{_mutex};
            auto existingWd = -1;

            // Check if this flow is already being watched
            for (auto const& [wd, rec] : _watches)
            {
                if (rec == record)
                {
                    existingWd = wd;
                    record.flowData = rec.flowData;
                    break;
                }
            }

            if (existingWd == -1)
            {
                MXL_DEBUG("Record for {} not found, creating one.", uuids::to_string(record.id));

#ifdef __APPLE__
                existingWd = ::open(record->fileName.c_str(), O_EVTONLY);
#elif defined __linux__
                // if not found, add the watch and add it to the maps
                existingWd = ::inotify_add_watch(_inotifyFd, record.fileName.c_str(), IN_ACCESS | IN_ATTRIB);
#endif
                if (existingWd == -1)
                {
                    auto const error = errno;
                    MXL_ERROR("Failed to add watch for file '{}': {}", record.fileName, std::strerror(error));
                    throw std::system_error{error, std::generic_category(), "Failed to add watch for file: " + record.fileName};
                }
                else
                {
                    MXL_DEBUG("Added watch {} for file: {}", existingWd, record.fileName);
                }
            }

            record.flowData = std::make_shared<DiscreteFlowData>(
                makeFlowDataFilePath(_domain, uuids::to_string(record.id)).c_str(), AccessMode::READ_WRITE, LockMode::None);
            _watches.emplace(existingWd, std::move(record));
        }
    }

    void DomainWatcher::removeFlow(DiscreteFlowWriter* writer, uuids::uuid id)
    {
        auto record = DomainWatcherRecord{
            .id = id,
            .fileName = makeFlowAccessFilePath(makeFlowDirectoryName(_domain, uuids::to_string(id))),
            .fw = writer,
            .flowData = {},
        };
        {
            auto lock = std::lock_guard{_mutex};

            // Remove the record for this writer
            auto it = std::ranges::find_if(_watches, [record](auto const& item) { return item.second == record; });
            if (it == _watches.end())
            {
                return;
            }

            auto const wd = it->first;
            _watches.erase(it);

            // Remove the watch if there are no more writers interested in notification for this flow.
            if (!_watches.contains(wd))
            {
#ifdef __APPLE__
                if (::close(wd) == -1)
                {
                    MXL_ERROR("Error closing file descriptor {} for '{}'", wd, rec->fileName);
                }
#elif defined __linux__
                if (::inotify_rm_watch(_inotifyFd, wd) == -1)
                {
                    auto const error = errno;
                    if ((error != EINVAL) || std::filesystem::exists(it->second.fileName))
                    {
                        MXL_WARN("Failed to remove inotify watch (wd={}) for '{}': {}", wd, record.fileName, std::strerror(error));
                    }
                }
#endif
            }
        }
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
                    it->second.fw->flowRead();
                }
            }
        }

#elif defined __linux__
        epoll_event events[1];
        alignas(alignof(struct inotify_event)) char buffer[4096] = {};

        while (_running)
        {
            int nfds = ::epoll_wait(_epollFd, events, 1, 250);
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
            auto length = ::read(_inotifyFd, buffer, sizeof buffer);
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

            processEventBuffer(reinterpret_cast<::inotify_event const*>(buffer), length / sizeof(::inotify_event));
        }
#endif
    }

#ifdef __linux__
    void DomainWatcher::processEventBuffer(::inotify_event const* buffer, std::size_t count)
    {
        auto lock = std::lock_guard{_mutex};
        auto time = currentTime(Clock::TAI);
        for (auto p = buffer; p != buffer + count; ++p)
        {
            if ((p->mask & (IN_ACCESS | IN_MODIFY | IN_ATTRIB)) == 0)
            {
                continue;
            }

            auto [it, _] = _watches.equal_range(p->wd);
            if (it == _watches.end())
            {
                continue;
            }

            try
            {
                auto& [_, record] = *it;
                record.flowData->flowInfo()->runtime.lastReadTime = time.value;
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
#elif defined __APPLE__
    // TODO
#endif

} // namespace mxl::lib
