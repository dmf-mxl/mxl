#include "DomainWatcher.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include "Logging.hpp"
#include "PathUtils.hpp"

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
#ifdef __APPLE__
        /* Open a kernel queue. */
        if ((_kq = kqueue()) < 0)
        {
            throw std::runtime_error("Failed to create a kqueue");
        }
#elif defined __linux__
        _inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        _epollFd = epoll_create1(0);

        if (_inotifyFd == -1 || _epollFd == -1)
        {
            throw std::runtime_error("Failed to initialize inotify or epoll");
        }

        // Add inotify file descriptor to epoll
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = _inotifyFd;
        if (epoll_ctl(_epollFd, EPOLL_CTL_ADD, _inotifyFd, &event) == -1)
        {
            throw std::runtime_error("Failed to add inotify FD to epoll");
        }
#endif

        // Start event processing thread
        _watchThread = std::thread{&DomainWatcher::processEvents, this};
    }

    DomainWatcher::~DomainWatcher()
    {
        _running = false;
        if (_watchThread.joinable())
        {
            _watchThread.join();
        }

#ifdef __APPLE__
        close(_kq);
#elif defined __linux__
        for (const auto& [wd, rec] : _watches)
        {
            inotify_rm_watch(_inotifyFd, wd);
        }
        close(_inotifyFd);
        close(_epollFd);
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
                throw std::runtime_error("Failed to add watch for file: " + record->fileName);
            }
            else
            {
                MXL_DEBUG("Added watch {} for file: {}", wd, record->fileName);
            }
            _watches.insert({wd, record});
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

        for (auto const& [wd, rec] : _watches)
        {
            if (*rec == *record)
            {
                if (rec->count == 1)
                {
#ifdef __APPLE__
                    close(wd);
#elif defined __linux__
                    inotify_rm_watch(_inotifyFd, wd);
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
                MXL_ERROR("kevent error {}", strerror(errno));
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
        alignas(alignof(struct inotify_event)) char buffer[406] = {};

        while (_running)
        {
            int nfds = epoll_wait(_epollFd, events, 1, 250);
            if (nfds <= 0)
            {
                continue;
            }

            ssize_t length = read(_inotifyFd, buffer, sizeof(buffer));
            if (length <= 0 || !_running)
            {
                continue;
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
                        _callback(it->second->id, it->second->type);
                    }
                }
                ptr += sizeof(struct inotify_event) + event->len;
            }
        }
#endif
    }

} // namespace mxl::lib
