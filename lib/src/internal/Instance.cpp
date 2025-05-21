#include "Instance.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <picojson/picojson.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include "DomainWatcher.hpp"
#include "FlowManager.hpp"
#include "FlowParser.hpp"
#include "Logging.hpp"
#include "PathUtils.hpp"
#include "PosixFlowReader.hpp"
#include "PosixFlowWriter.hpp"

namespace mxl::lib
{

    namespace
    {

        std::once_flag loggingFlag;

        void initializeLogging()
        {
            auto console = spdlog::stdout_color_mt("console");
            spdlog::set_default_logger(console);
            spdlog::cfg::load_env_levels("MXL_LOG_LEVEL");
        }

    } // namespace

    Instance::Instance(std::filesystem::path const& in_mxlDomain, std::string const& in_options)
        : _readers{}
        , _writers{}
        , _mutex{}
        , _options{in_options}
        , _flowManager{}
        , _historyDuration{100'000'000ULL}
        , _watcher{}
        , _stopping{false}
    {
        std::call_once(loggingFlag, [&]() { initializeLogging(); });
        _flowManager = std::make_shared<FlowManager>(in_mxlDomain);
        _watcher = std::make_shared<DomainWatcher>(in_mxlDomain,
            [this](auto const& in_uuid, auto in_type) { fileChangedCallback(in_uuid, in_type); });
        MXL_DEBUG("Instance created. MXL Domain: {}", in_mxlDomain.string());
    }

    Instance::~Instance()
    {
        _stopping = true;
        _watcher->stop();
        MXL_DEBUG("Instance destroyed.");
        spdlog::default_logger()->flush();
    }

    void Instance::fileChangedCallback(uuids::uuid const& in_flowId, WatcherType in_type)
    {
        if (!_stopping)
        {
            std::lock_guard<std::mutex> lock(_mutex);

            // Someone wrote to the flow. let the readers know.
            if (in_type == WatcherType::WRITER)

            {
                // Someone read the grain and touched the ".access" file.  let update the last read count.
                if (auto const found = _writers.find(in_flowId); found != _writers.end())
                {
                    found->second.get()->flowRead();
                }
            }
        }
    }

    FlowReader* Instance::getFlowReader(std::string const& flowId)
    {
        auto const id = uuids::uuid::from_string(flowId);
        // FIXME: Check result of the from_string operation.

        auto const lock = std::lock_guard<std::mutex>{_mutex};
        if (auto const pos = _readers.lower_bound(*id); pos != _readers.end())
        {
            auto& v = (*pos).second;
            v.addReference();
            return v.get();
        }
        else
        {
            auto reader = std::make_unique<PosixFlowReader>(_flowManager, *id);
            if (reader->open())
            {
                // FIXME: This leaks if the map insertion throws an exception.
                //     Delegate the watch handling to the reader itself by
                //     passing it a reference to the DomainWatcher.
                _watcher->addFlow(*id, WatcherType::READER);
                return (*_readers.try_emplace(pos, *id, std::move(reader))).second.get();
            }
            throw std::runtime_error("Failed to open flow " + flowId);
        }
    }

    void Instance::releaseReader(FlowReader* reader)
    {
        if (reader)
        {
            auto const& id = reader->getId();

            auto const lock = std::lock_guard<std::mutex>{_mutex};
            if (auto const pos = _readers.find(id); pos != _readers.end())
            {
                if ((*pos).second.releaseReference())
                {
                    _watcher->removeFlow(id, WatcherType::READER);
                    _readers.erase(pos);
                }
            }
        }
    }

    FlowWriter* Instance::getFlowWriter(std::string const& flowId)
    {
        auto const id = uuids::uuid::from_string(flowId);
        // FIXME: Check result of the from_string operation.

        auto const lock = std::lock_guard<std::mutex>{_mutex};
        if (auto const pos = _writers.lower_bound(*id); pos != _writers.end())
        {
            auto& v = (*pos).second;
            v.addReference();
            return v.get();
        }
        else
        {
            auto writer = std::make_unique<PosixFlowWriter>(_flowManager, *id);
            if (writer->open())
            {
                // FIXME: This leaks if the map insertion throws an exception.
                //     Delegate the watch handling to the writer itself by
                //     passing it a reference to the DomainWatcher.
                _watcher->addFlow(*id, WatcherType::WRITER);
                return (*_writers.try_emplace(pos, *id, std::move(writer))).second.get();
            }
            throw std::runtime_error("Failed to open flow " + flowId);
        }
    }

    void Instance::releaseWriter(FlowWriter* writer)
    {
        if (writer)
        {
            auto const& id = writer->getId();

            auto const lock = std::lock_guard<std::mutex>{_mutex};
            if (auto const pos = _writers.find(id); pos != _writers.end())
            {
                if ((*pos).second.releaseReference())
                {
                    _watcher->removeFlow(id, WatcherType::WRITER);
                    _writers.erase(pos);
                }
            }
        }
    }

    std::unique_ptr<FlowData> Instance::createFlow(std::string const& in_flowDef)
    {
        // Parse the json flow resource
        auto const parser = FlowParser{in_flowDef};
        // Read the mandatory grain_rate field
        auto const grainRate = parser.getGrainRate();
        // Compute the grain count based on our configured history duration
        auto const grainCount = _historyDuration * grainRate.numerator / (1'000'000'000ULL * grainRate.denominator);

        // Create the flow using the flow manager
        return _flowManager->createFlow(parser.getId(), in_flowDef, grainCount, grainRate, parser.getPayloadSize());
    }

    bool Instance::deleteFlow(uuids::uuid const& in_id)
    {
        return _flowManager->deleteFlow(in_id);
    }

    // This function is performed in a 'collaborative best effort' way.
    // Exceptions thrown should not be propagated to the caller and cause disruptions to the application.
    // On error the function will return 0 and log the error
    std::size_t Instance::garbageCollect() const
    {
        std::size_t count = 0;

        try
        {
            auto base = std::filesystem::path{_flowManager->getDomain()};
            if (exists(base) && is_directory(base))
            {
                for (auto const& entry : std::filesystem::directory_iterator{base})
                {
                    if (is_directory(entry) && (entry.path().extension() == mxl::lib::FLOW_DIRECTORY_NAME_SUFFIX))
                    {
                        // Try to obtain an exclusive lock on the flow data file.  If we can obtain one it means that no
                        // other process is writing to the flow.
                        auto const flowDataFile = mxl::lib::makeFlowDataFilePath(_flowManager->getDomain(), entry.path().stem().string());

                        // Check if the flow data file exists
                        if (!std::filesystem::exists(flowDataFile))
                        {
                            MXL_DEBUG("Flow data file {} does not exist", flowDataFile.string());
                            continue;
                        }

                        // Open a file descriptor to the flow data file
                        int flags = O_RDONLY | O_CLOEXEC;
#ifndef __APPLE__
                        flags |= O_NOATIME;
#endif
                        int fd = ::open(flowDataFile.c_str(), flags);
                        // Try to obtain an exclusive lock on the file descriptor. Do not block if the lock cannot be obtained.
                        bool active = ::flock(fd, LOCK_EX | LOCK_NB) < 0;
                        ::close(fd);

                        // The flow is not active.  remove it (the folder and everything in it)
                        if (!active)
                        {
                            std::error_code ec;
                            std::filesystem::remove_all(entry.path(), ec);
                            if (ec)
                            {
                                MXL_DEBUG("Failed to remove '{}': {} (error code {})", entry.path().string(), ec.message(), ec.value());
                            }
                            else
                            {
                                count++;
                            }
                        }
                    }
                }
            }
            else
            {
                MXL_DEBUG("MXL domain {} does not exist or is not a directory", base.string());
            }
        }
        catch (std::exception const& e)
        {
            MXL_DEBUG("Failed to perform garbage collection: {}", e.what());
        }
        catch (...)
        {
            MXL_DEBUG("Failed to perform garbage collection");
        }
        return count;
    }

} // namespace mxl::lib
