#include "Instance.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <picojson/picojson.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "DomainWatcher.hpp"
#include "FlowManager.hpp"
#include "FlowParser.hpp"
#include "Logging.hpp"
#include "PathUtils.hpp"
#include "PosixDiscreteFlowReader.hpp"
#include "PosixDiscreteFlowWriter.hpp"

namespace mxl::lib
{
    namespace
    {
        constexpr auto MXL_HISTORY_DURATION_OPTION = "urn:x-mxl:option:history_duration/v1.0";

        std::once_flag loggingFlag;

        void initializeLogging()
        {
            auto console = spdlog::stdout_color_mt("console");
            spdlog::set_default_logger(console);
            spdlog::cfg::load_env_levels("MXL_LOG_LEVEL");
        }

        /// Simple json string parser wrapper
        ///
        /// \param options The options json string to parse
        /// \param config The parsed json object
        /// \return true if the parsing was successful, false otherwise
        bool parseOptionsJson(std::string const& options, picojson::object& config)
        {
            picojson::value parsed;
            std::string err = picojson::parse(parsed, options);
            if (!err.empty())
            {
                MXL_ERROR("Failed to parse options json: {}", err);
                return false;
            }

            if (!parsed.is<picojson::object>())
            {
                return false;
            }

            // Assign the json object config
            config = parsed.get<picojson::object>();

            return true;
        }

    }

    Instance::Instance(std::filesystem::path const& mxlDomain, std::string const& options, std::unique_ptr<FlowIoFactory>&& flowIoFactory)
        : _flowManager{mxlDomain}
        , _flowIoFactory{std::move(flowIoFactory)}
        , _readers{}
        , _writers{}
        , _mutex{}
        , _options{options}
        , _historyDuration{100'000'000ULL}
        , _watcher{}
        , _stopping{false}
    {
        std::call_once(loggingFlag, [&]() { initializeLogging(); });
        parseOptions(options);
        _watcher = std::make_shared<DomainWatcher>(mxlDomain, [this](auto const& uuid, auto type) { fileChangedCallback(uuid, type); });
        MXL_DEBUG("Instance created. MXL Domain: {}", mxlDomain.string());
    }

    Instance::~Instance()
    {
        _stopping = true;
        _watcher->stop();
        MXL_DEBUG("Instance destroyed.");
        spdlog::default_logger()->flush();
    }

    void Instance::fileChangedCallback(uuids::uuid const& flowId, WatcherType type)
    {
        if (!_stopping)
        {
            std::lock_guard<std::mutex> lock(_mutex);

            // Someone wrote to the flow. let the readers know.
            if (type == WatcherType::WRITER)

            {
                // Someone read the grain and touched the ".access" file.  let update the last read count.
                if (auto const found = _writers.find(flowId); found != _writers.end())
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

        auto const lock = std::lock_guard{_mutex};
        if (auto const pos = _readers.lower_bound(*id); pos != _readers.end())
        {
            auto& v = (*pos).second;
            v.addReference();
            return v.get();
        }
        else
        {
            auto flowData = _flowManager.openFlow(*id, AccessMode::READ_ONLY);
            auto reader = _flowIoFactory->createFlowReader(_flowManager, *id, std::move(flowData));

            if (dynamic_cast<ContinuousFlowReader*>(reader.get()) == nullptr)
            {
                // FIXME: This leaks if the map insertion throws an exception.
                //     Delegate the watch handling to the reader itself by
                //     passing it a reference to the DomainWatcher.
                //
                //     Doing it like this would also get rid of the ugly cast
                //     to decide whether or not to install the watch.
                _watcher->addFlow(*id, WatcherType::READER);
            }
            return (*_readers.try_emplace(pos, *id, std::move(reader))).second.get();
        }
    }

    void Instance::releaseReader(FlowReader* reader)
    {
        if (reader)
        {
            auto const& id = reader->getId();

            auto const lock = std::lock_guard{_mutex};
            if (auto const pos = _readers.find(id); pos != _readers.end())
            {
                if ((*pos).second.releaseReference())
                {
                    if (dynamic_cast<ContinuousFlowReader*>((*pos).second.get()) == nullptr)
                    {
                        _watcher->removeFlow(id, WatcherType::READER);
                    }
                    _readers.erase(pos);
                }
            }
        }
    }

    FlowWriter* Instance::getFlowWriter(std::string const& flowId)
    {
        auto const id = uuids::uuid::from_string(flowId);
        // FIXME: Check result of the from_string operation.

        auto const lock = std::lock_guard{_mutex};
        if (auto const pos = _writers.lower_bound(*id); pos != _writers.end())
        {
            auto& v = (*pos).second;
            v.addReference();
            return v.get();
        }
        else
        {
            auto flowData = _flowManager.openFlow(*id, AccessMode::READ_WRITE);
            auto writer = _flowIoFactory->createFlowWriter(_flowManager, *id, std::move(flowData));

            if (dynamic_cast<ContinuousFlowWriter*>(writer.get()) == nullptr)
            {
                // FIXME: This leaks if the map insertion throws an exception.
                //     Delegate the watch handling to the writer itself by
                //     passing it a reference to the DomainWatcher.
                //
                //     Doing it like this would also get rid of the ugly cast
                //     to decide whether or not to install the watch.
                _watcher->addFlow(*id, WatcherType::WRITER);
            }
            return (*_writers.try_emplace(pos, *id, std::move(writer))).second.get();
        }
    }

    void Instance::releaseWriter(FlowWriter* writer)
    {
        if (writer)
        {
            auto const& id = writer->getId();
            auto removeFlowWatch = false;
            {
                auto const lock = std::lock_guard{_mutex};
                if (auto const pos = _writers.find(id); pos != _writers.end())
                {
                    if ((*pos).second.releaseReference())
                    {
                        removeFlowWatch = (dynamic_cast<ContinuousFlowWriter*>((*pos).second.get()) == nullptr);
                        _writers.erase(pos);
                    }
                }
            }
            if (removeFlowWatch)
            {
                _watcher->removeFlow(id, WatcherType::WRITER);
            }
        }
    }

    std::unique_ptr<FlowData> Instance::createFlow(std::string const& flowDef)
    {
        // Parse the json flow resource
        auto const parser = FlowParser{flowDef};

        // Create the flow using the flow manager
        if (auto const format = parser.getFormat(); mxlIsDiscreteDataFormat(format))
        {
            // Read the mandatory grain_rate field
            auto const grainRate = parser.getGrainRate();
            // Compute the grain count based on our configured history duration
            auto const grainCount = _historyDuration * grainRate.numerator / (1'000'000'000ULL * grainRate.denominator);

            return _flowManager.createDiscreteFlow(parser.getId(), flowDef, parser.getFormat(), grainCount, grainRate, parser.getPayloadSize());
        }
        else if (mxlIsContinuousDataFormat(format))
        {
            // Read the mandatory grain_rate field
            auto const sampleRate = parser.getGrainRate();
            // Compute the grain count based on our configured history duration
            auto const bufferLength = _historyDuration * sampleRate.numerator / (1'000'000'000ULL * sampleRate.denominator);

            auto const sampleWordSize = parser.getPayloadSize();
            // FIXME: The page size is just an educated guess to round to for good measure
            auto const lengthPerPage = 4096U / sampleWordSize;

            auto const pageAlignedLength = ((bufferLength + lengthPerPage - 1U) / lengthPerPage) * lengthPerPage;

            return _flowManager.createContinuousFlow(
                parser.getId(), flowDef, parser.getFormat(), sampleRate, parser.getChannelCount(), sampleWordSize, pageAlignedLength);
        }
        throw std::runtime_error("Unsupported flow format.");
    }

    bool Instance::deleteFlow(uuids::uuid const& flowId)
    {
        return _flowManager.deleteFlow(flowId);
    }

    // This function is performed in a 'collaborative best effort' way.
    // Exceptions thrown should not be propagated to the caller and cause disruptions to the application.
    // On error the function will return 0 and log the error
    std::size_t Instance::garbageCollect() const
    {
        std::size_t count = 0;

        try
        {
            auto base = std::filesystem::path{_flowManager.getDomain()};
            if (exists(base) && is_directory(base))
            {
                for (auto const& entry : std::filesystem::directory_iterator{base})
                {
                    if (is_directory(entry) && (entry.path().extension() == mxl::lib::FLOW_DIRECTORY_NAME_SUFFIX))
                    {
                        // Try to obtain an exclusive lock on the flow data file.  If we can obtain one it means that no
                        // other process is writing to the flow.
                        auto const flowDataFile = mxl::lib::makeFlowDataFilePath(_flowManager.getDomain(), entry.path().stem().string());

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

    void Instance::parseOptions(std::string const& options)
    {
        // This could be way more sophisticated, but for now we just parse the options and extract the history_duration value if it is present.
        // A full configuration framework with validation, defaults, etc. could be implemented at a later point.

        // Start with the default history duration
        std::uint64_t historyDuration = _historyDuration;

        //
        // Try to parse the .options file found in the MXL domain directory.
        // If found and configured, it will override the default history duration.
        //
        auto domainOptionsFile = _flowManager.getDomain() / ".options";
        if (exists(domainOptionsFile))
        {
            std::ifstream ifs(domainOptionsFile);
            if (!ifs)
            {
                MXL_ERROR("Failed to open domain options file: {}", domainOptionsFile.string());
            }
            else
            {
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                std::string json_content = buffer.str();
                picojson::object config;
                if (parseOptionsJson(json_content, config))
                {
                    if (auto it = config.find(MXL_HISTORY_DURATION_OPTION); it != config.end() && it->second.is<double>())
                    {
                        MXL_TRACE("Found history duration option in domain specific options: {}ns", it->second.get<double>());
                        historyDuration = static_cast<std::uint64_t>(it->second.get<double>());
                    }
                }
                else
                {
                    MXL_ERROR("Failed to parse domain specific options: {}", options);
                }
            }
        }

        // If we have an instance level options string, parse it as well.
        if (!options.empty())
        {
            picojson::object config;
            if (parseOptionsJson(options, config))
            {
                // We are not considering MXL_HISTORY_DURATION_TAG here. we don't want per-instance history durations.
                // In the future we might want to use this mecanism to set other options that would be instance specific.
            }
            else
            {
                MXL_ERROR("Failed to parse instance specific options: {}", options);
            }
        }

        // Set the history duration
        _historyDuration = historyDuration;
        MXL_DEBUG("History duration set to {} ns", historyDuration);
    }

    std::uint64_t Instance::getHistoryDurationNs() const
    {
        return _historyDuration;
    }

} // namespace mxl::lib
