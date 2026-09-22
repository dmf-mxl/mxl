// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/Instance.hpp"
#include <cerrno>
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
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <picojson/wrapper.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/FlowOptionsParser.hpp"
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

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

        /**
         * @brief Read the domain history duration, retaining the fallback when no valid option is present.
         * @param path Domain options file to inspect.
         * @param fallback History duration in nanoseconds used when the option is absent or unreadable.
         * @return Configured history duration in nanoseconds, or fallback.
         * @throws std::exception Filesystem access or allocation fails.
         */
        std::uint64_t readDomainHistoryDuration(std::filesystem::path const& path, std::uint64_t fallback)
        {
            auto const file = std::ifstream{path};
            if (!file)
            {
                // Missing options are expected; report other open failures.
                if (errno != ENOENT)
                {
                    MXL_ERROR("Failed to open domain options file: {}", path.string());
                }
                return fallback;
            }
            auto buffer = std::stringstream{};
            buffer << file.rdbuf();
            auto const json = buffer.str();
            auto config = picojson::object{};
            if (!parseOptionsJson(json, config))
            {
                MXL_ERROR("Failed to parse domain specific options: {}", json);
                return fallback;
            }
            auto const it = config.find(MXL_HISTORY_DURATION_OPTION);
            if ((it == config.end()) || !it->second.is<double>())
            {
                return fallback;
            }
            MXL_TRACE("Found history duration option in domain specific options: {}ns", it->second.get<double>());
            return static_cast<std::uint64_t>(it->second.get<double>());
        }

    }

    Instance::Instance(std::filesystem::path const& mxlDomain, std::string const& options, std::unique_ptr<FlowIoFactory>&& flowIoFactory,
        DomainWatcher::ptr watcher)
        : _flowManager{mxlDomain}
        , _flowIoFactory{std::move(flowIoFactory)}
        , _readers{}
        , _writers{}
        , _mutex{}
        , _syncGroups{}
        , _options{options}
        , _historyDuration{200'000'000ULL}
        , _watcher{std::move(watcher)}
        , _stopping{false}
    {
        std::call_once(loggingFlag, [&]() { initializeLogging(); });
        parseOptions(options);
        MXL_DEBUG("Instance created. MXL Domain: {}", mxlDomain.string());
    }

    Instance::~Instance()
    {
        _stopping = true;
        _watcher->stop();
        MXL_DEBUG("Instance destroyed.");

        for (auto& [id, writer] : _writers)
        {
            try
            {
                if (writer.get()->isExclusive() || writer.get()->makeExclusive())
                {
                    MXL_WARN("Cleaning up flow '{}' of leaked flow writer.", uuids::to_string(id));

                    _flowManager.deleteFlow(id);
                }
            }
            catch (std::exception const& ex)
            {
                MXL_ERROR("Failed to clean up leaked flow writer: {}", ex.what());
            }
        }

        spdlog::default_logger()->flush();
    }

    FlowReader* Instance::getFlowReader(std::string const& flowId)
    {
        auto const id = uuids::uuid::from_string(flowId);
        if (!id.has_value())
        {
            throw std::invalid_argument{"Invalid flow UUID."};
        }

        auto const lock = std::scoped_lock{_mutex};
        if (auto const pos = _readers.find(*id); pos != _readers.end())
        {
            auto& v = (*pos).second;
            v.addReference();
            return v.get();
        }
        else
        {
            auto flowData = _flowManager.openFlow(*id, AccessMode::READ_ONLY);
            auto reader = _flowIoFactory->createFlowReader(_flowManager, *id, std::move(flowData));

            return (*_readers.try_emplace(pos, *id, std::move(reader))).second.get();
        }
    }

    void Instance::releaseReader(FlowReader* reader)
    {
        if (reader)
        {
            auto const& id = reader->getId();

            auto const lock = std::scoped_lock{_mutex};
            if (auto const pos = _readers.find(id); pos != _readers.end())
            {
                if ((*pos).second.releaseReference())
                {
                    // Remove from the synchronization groups
                    for (auto& group : _syncGroups)
                    {
                        group.removeReader(*reader);
                    }
                    _readers.erase(pos);
                }
            }
        }
    }

    void Instance::releaseWriter(FlowWriter* writer)
    {
        if (writer)
        {
            auto const id = writer->getId();
            {
                auto const lock = std::scoped_lock{_mutex};
                if (auto const pos = _writers.find(id); pos != _writers.end())
                {
                    if ((*pos).second.releaseReference())
                    {
                        // Delete the flow if we are the last writer.
                        if (writer->isExclusive() || writer->makeExclusive())
                        {
                            _flowManager.deleteFlow(id);
                        }

                        _writers.erase(pos);
                    }
                }
            }
        }
    }

    std::tuple<mxlFlowConfigInfo, FlowWriter*, bool> Instance::createFlowWriter(std::string const& flowDef, std::optional<std::string> options)
    {
        auto const lock = std::scoped_lock{_mutex};
        auto const parser = FlowParser{flowDef};
        auto const optionsParser = (options) ? FlowOptionsParser{*options} : FlowOptionsParser{};
        auto created = false;
        auto flowData = std::unique_ptr<FlowData>{};

        if (auto const format = parser.getFormat(); mxlIsDiscreteDataFormat(format))
        {
            auto [rFlowData, rCreated] = createOrOpenDiscreteFlowData(flowDef, parser, optionsParser);
            flowData = std::move(rFlowData);
            created = rCreated;
        }
        else if (format == MXL_DATA_FORMAT_EVENT)
        {
            auto const grainRate = parser.getGrainRate();
            auto const eventCount = _historyDuration * __int128_t{grainRate.numerator} / (1'000'000'000 * __int128_t{grainRate.denominator});
            if ((eventCount < 2) || (eventCount > 65536))
            {
                throw std::invalid_argument("Invalid event count.");
            }
            auto [wasCreated, data] = _flowManager.createOrOpenEventFlow(
                parser.getId(), flowDef, static_cast<std::size_t>(eventCount), grainRate, parser.getPayloadSize());
            flowData = std::move(data);
            created = wasCreated;
        }
        else if (mxlIsContinuousDataFormat(format))
        {
            auto [rFlowData, rWasCreated] = createOrOpenContinuousFlowData(flowDef, parser, optionsParser);
            flowData = std::move(rFlowData);
            created = rWasCreated;
        }
        else
        {
            throw std::runtime_error("Invalid flow format.");
        }

        auto id = uuids::uuid{flowData->flowInfo()->config.common.id};
        auto flowConfigInfo = flowData->flowInfo()->config;

        if (auto const pos = _writers.find(id); pos != _writers.end())
        {
            auto& v = (*pos).second;
            v.addReference();
            return {flowConfigInfo, v.get(), created};
        }
        else
        {
            auto writer = _flowIoFactory->createFlowWriter(_flowManager, id, std::move(flowData));

            return {flowConfigInfo, (*_writers.try_emplace(pos, id, std::move(writer))).second.get(), created};
        }
    }

    std::pair<std::unique_ptr<FlowData>, bool> Instance::createOrOpenDiscreteFlowData(std::string const& flowDef, FlowParser const& parser,
        FlowOptionsParser const& optionsParser)
    {
        // Read the mandatory grain_rate field
        auto const grainRate = parser.getGrainRate();
        // Compute the grain count based on our configured history duration
        auto const grainCount = _historyDuration * grainRate.numerator / (1'000'000'000ULL * grainRate.denominator);

        auto const batchSizeDefault = parser.getTotalPayloadSlices();

        auto [created, flowData] = _flowManager.createOrOpenDiscreteFlow(parser.getId(),
            flowDef,
            parser.getFormat(),
            grainCount,
            grainRate,
            parser.getPayloadSize(),
            parser.getTotalPayloadSlices(),
            parser.getPayloadSliceLengths(),
            optionsParser.getMaxSyncBatchSizeHint().value_or(batchSizeDefault),
            optionsParser.getMaxCommitBatchSizeHint().value_or(batchSizeDefault));

        return {std::move(flowData), created};
    }

    std::pair<std::unique_ptr<FlowData>, bool> Instance::createOrOpenContinuousFlowData(std::string const& flowDef, FlowParser const& parser,
        FlowOptionsParser const& optionsParser)
    {
        // Read the mandatory grain_rate field
        auto const sampleRate = parser.getGrainRate();
        // Compute the buffer length based on our configured history duration.
        // The length is divided by 500M instead of 1B to effectively make it twice the
        // history duration, which is necessary, because only half of the buffer is
        // accessible for reading at any one point in time.
        auto const bufferLength = _historyDuration * sampleRate.numerator / (500'000'000ULL * sampleRate.denominator);

        auto const sampleWordSize = parser.getPayloadSize();
        // FIXME: The page size is just an educated guess to round to for good measure
        auto const lengthPerPage = 4096U / sampleWordSize;

        auto const pageAlignedLength = ((bufferLength + lengthPerPage - 1U) / lengthPerPage) * lengthPerPage;

        // Default to 10ms worth of samples
        auto batchSizeDefault = parser.getGrainRate().numerator / (100U * parser.getGrainRate().denominator);

        auto [created, flowData] = _flowManager.createOrOpenContinuousFlow(parser.getId(),
            flowDef,
            parser.getFormat(),
            sampleRate,
            parser.getChannelCount(),
            sampleWordSize,
            pageAlignedLength,
            optionsParser.getMaxSyncBatchSizeHint().value_or(batchSizeDefault),
            optionsParser.getMaxCommitBatchSizeHint().value_or(batchSizeDefault));

        return {std::move(flowData), created};
    }

    std::string Instance::getDomain() const
    {
        return _flowManager.getDomain();
    }

    std::string Instance::getFlowDef(uuids::uuid const& flowId) const
    {
        return _flowManager.getFlowDef(flowId);
    }

    // This function is performed in a 'collaborative best effort' way.
    // Exceptions thrown should not be propagated to the caller and cause disruptions to the application.
    // On error the function will return 0 and log the error
    std::size_t Instance::garbageCollect() const
    {
        auto count = std::size_t{};
        try
        {
            auto const base = std::filesystem::path{_flowManager.getDomain()};
            if (!is_directory(base))
            {
                MXL_DEBUG("MXL domain {} does not exist or is not a directory", base.string());
                return count;
            }
            for (auto const& entry : std::filesystem::directory_iterator{base})
            {
                if (!is_directory(entry) || (entry.path().extension() != mxl::lib::FLOW_DIRECTORY_NAME_SUFFIX))
                {
                    continue;
                }
                auto const flowDataFile = mxl::lib::makeFlowDataFilePath(_flowManager.getDomain(), entry.path().stem().string());
                if (!std::filesystem::exists(flowDataFile))
                {
                    MXL_DEBUG("Flow data file {} does not exist", flowDataFile.string());
                    continue;
                }

#ifdef __APPLE__
                constexpr auto flags = O_RDONLY | O_CLOEXEC;
#else
                constexpr auto flags = O_RDONLY | O_CLOEXEC | O_NOATIME;
#endif
                auto const fd = ::open(flowDataFile.c_str(), flags);
                // An exclusive lock indicates that no producer is using the flow.
                auto const active = ::flock(fd, LOCK_EX | LOCK_NB) < 0;
                ::close(fd);
                if (active)
                {
                    continue;
                }
                auto ec = std::error_code{};
                std::filesystem::remove_all(entry.path(), ec);
                if (ec)
                {
                    MXL_DEBUG("Failed to remove '{}': {} (error code {})", entry.path().string(), ec.message(), ec.value());
                    continue;
                }
                ++count;
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
        _historyDuration = readDomainHistoryDuration(makeDomainOptionsFilePath(_flowManager.getDomain()), _historyDuration);

        // Instance options are validated but do not override domain history duration.
        if (!options.empty())
        {
            auto config = picojson::object{};
            if (!parseOptionsJson(options, config))
            {
                MXL_ERROR("Failed to parse instance specific options: {}", options);
            }
        }
        MXL_DEBUG("History duration set to {} ns", _historyDuration);
    }

    std::uint64_t Instance::getHistoryDurationNs() const
    {
        return _historyDuration;
    }

    FlowSynchronizationGroup* Instance::createFlowSynchronizationGroup()
    {
        return &_syncGroups.emplace_front();
    }

    void Instance::releaseFlowSynchronizationGroup(FlowSynchronizationGroup const* group)
    {
        auto prev = _syncGroups.before_begin();
        for (auto current = std::next(prev); current != _syncGroups.end(); ++current)
        {
            if (&(*current) == group)
            {
                _syncGroups.erase_after(prev);
                return;
            }
            prev = current;
        }
    }
} // namespace mxl::lib
