// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowManager.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <picojson/picojson.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/PayloadBackend.hpp"
#include "mxl-internal/PayloadStorage.hpp"
#include "mxl-internal/SharedMemory.hpp"
#include "mxl-internal/Timing.hpp"
#include "Deferred.hpp"
#include "DynamicPointerCast.hpp"

namespace mxl::lib
{
    namespace
    {
        /**
         * Attempt to create a temporary directory to prepare a new flow.
         * The temporary name is structured in a way that prevents is from
         * clashing with directory names that belong to established flows.
         *
         * \param[in] base the base directory, below which the temporary
         *      directory should be created,
         * \return A filesystem path referring to the newly created temporary
         *      directory.
         * \throws std::filesystem::filesystem_error if creating the temporary
         *      directory failed for whatever reason.
         */
        std::filesystem::path createTemporaryFlowDirectory(std::filesystem::path const& base)
        {
            auto pathBuffer = (base / ".mxl-tmp-XXXXXXXXXXXXXXXX").string();
            if (::mkdtemp(pathBuffer.data()) == nullptr)
            {
                auto const error = errno;
                MXL_ERROR("mkdtemp failed for path '{}' (errno {}: {}).", base.string(), error, std::strerror(error));
                throw std::filesystem::filesystem_error{
                    "Could not create temporary directory.", base, std::error_code{error, std::generic_category()}
                };
            }
            return pathBuffer;
        }

        bool publishFlowDirectory(std::filesystem::path const& source, std::filesystem::path const& dest)
        {
            permissions(source,
                std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                    std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add);

#if defined __linux__
            if (::renameat2(AT_FDCWD, source.c_str(), AT_FDCWD, dest.c_str(), RENAME_NOREPLACE) < 0)
#elif defined __APPLE__
            if (::renamex_np(source.c_str(), dest.c_str(), RENAME_EXCL) < 0)
#endif
            {
                auto const error = errno;
                switch (error)
                {
                    case EEXIST:    return false;
                    case ENOTEMPTY: return false;
                    default:
                        throw std::system_error{error, std::system_category(), "Failed to publish flow directory by renaming it to its public name."};
                }
            }

            return true;
        }

        /**
         * Sanitize the passed format by mapping all currently unsupported
         * formats to MXL_DATA_FORMAT_UNSPECIFIED.
         */
        mxlDataFormat sanitizeFlowFormat(mxlDataFormat format)
        {
            return mxlIsSupportedDataFormat(format) ? format : MXL_DATA_FORMAT_UNSPECIFIED;
        }

        void writeFlowDescriptor(std::filesystem::path const& flowDir, std::string const& flowDef)
        {
            auto const flowJsonFile = makeFlowDescriptorFilePath(flowDir);
            if (auto out = std::ofstream{flowJsonFile, std::ios::out | std::ios::trunc}; out)
            {
                out << flowDef;
            }
            else
            {
                throw std::filesystem::filesystem_error{
                    "Failed to create flow resource definition.", flowJsonFile, std::make_error_code(std::errc::io_error)};
            }
        }

        mxlCommonFlowConfigInfo initCommonFlowConfigInfo(uuids::uuid const& flowId, mxlDataFormat format, mxlRational grainRate,
            std::uint32_t maxSyncBatchSizeHintOpt, std::uint32_t maxCommitBatchSizeHintOpt)
        {
            auto result = mxlCommonFlowConfigInfo{};

            auto const idSpan = flowId.as_bytes();
            std::memcpy(result.id, idSpan.data(), idSpan.size());
            result.format = format;
            result.grainRate = grainRate;

            result.maxCommitBatchSizeHint = maxCommitBatchSizeHintOpt;
            result.maxSyncBatchSizeHint = maxSyncBatchSizeHintOpt;

            // A backend reports its device once it has prepared the storage.
            result.deviceIndex = -1;

            return result;
        }

        mxlFlowRuntimeInfo initFlowRuntimeInfo()
        {
            auto result = mxlFlowRuntimeInfo{};

            result.lastWriteTime = currentTime(mxl::lib::Clock::TAI).value;
            result.lastReadTime = result.lastWriteTime;

            return result;
        }

        FlowState initFlowState(std::filesystem::path const& flowDataPath)
        {
            auto result = FlowState{};

            // Get the inode of the flow data file
            struct ::stat st;
            if (::stat(flowDataPath.string().c_str(), &st) != 0)
            {
                auto const error = errno;
                throw std::filesystem::filesystem_error{
                    "Could not stat flow data file.", flowDataPath, std::error_code{error, std::generic_category()}
                };
            }
            else
            {
                result.inode = st.st_ino;
            }

            return result;
        }
    }

    namespace
    {
        /** Version of the payload descriptor format written by this SDK. */
        constexpr auto PAYLOAD_DESCRIPTOR_VERSION = 1.0;

        /**
         * Contents of the payload descriptor of a flow.
         */
        struct PayloadDescriptor
        {
            std::string backend;       ///< The backend that holds the payload.
            std::uint32_t storageType; ///< The mxlPayloadStorageType the backend reported.
        };

        /**
         * Write the payload descriptor of a new flow.
         * \param[in] flowDir The temporary flow directory.
         * \param[in] storageSpec The storage requested by the writer.
         * \param[in] storageType The storage type the backend reports.
         */
        void writePayloadDescriptor(std::filesystem::path const& flowDir, PayloadStorageSpec const& storageSpec, std::uint32_t storageType)
        {
            auto root = picojson::object{};
            root["version"] = picojson::value{PAYLOAD_DESCRIPTOR_VERSION};
            root["backend"] = picojson::value{storageSpec.backend};
            root["storageType"] = picojson::value{static_cast<double>(storageType)};

            auto const path = makePayloadDescriptorFilePath(flowDir);
            if (auto out = std::ofstream{path, std::ios::out | std::ios::trunc}; out)
            {
                out << picojson::value{root}.serialize(true);
                if (out)
                {
                    return;
                }
            }
            throw std::filesystem::filesystem_error{"Failed to write the payload descriptor.", path, std::make_error_code(std::errc::io_error)};
        }

        /**
         * Read the payload descriptor of an existing flow.
         * \param[in] flowDir The flow directory.
         * \return The descriptor.
         * \throws PayloadStorageError with MXL_ERR_INVALID_STATE if the descriptor is missing or invalid, or with
         *     MXL_ERR_UNSUPPORTED_OPERATION if it has a version this SDK does not know.
         */
        PayloadDescriptor readPayloadDescriptor(std::filesystem::path const& flowDir)
        {
            auto const path = makePayloadDescriptorFilePath(flowDir);
            auto in = std::ifstream{path, std::ios::in};
            if (!in)
            {
                throw PayloadStorageError{MXL_ERR_INVALID_STATE, fmt::format("The flow has no readable payload descriptor at {}.", path.string())};
            }

            auto parsed = picojson::value{};
            auto const error = picojson::parse(parsed, in);
            if (!error.empty() || !parsed.is<picojson::object>())
            {
                throw PayloadStorageError{MXL_ERR_INVALID_STATE, fmt::format("The payload descriptor at {} is not a JSON object.", path.string())};
            }

            auto const& root = parsed.get<picojson::object>();
            // Writer and reader can come from different SDK releases, for example in different containers. A descriptor
            // written in another format is refused instead of misread.
            if (auto const version = root.find("version");
                (version == root.end()) || !version->second.is<double>() || (version->second.get<double>() != PAYLOAD_DESCRIPTOR_VERSION))
            {
                throw PayloadStorageError{MXL_ERR_UNSUPPORTED_OPERATION,
                    fmt::format("The payload descriptor at {} has a version this SDK does not support. Supported is: {}.",
                        path.string(),
                        PAYLOAD_DESCRIPTOR_VERSION)};
            }

            auto const backend = root.find("backend");
            auto const storageType = root.find("storageType");
            if ((backend == root.end()) || !backend->second.is<std::string>() || (storageType == root.end()) || !storageType->second.is<double>() ||
                (storageType->second.get<double>() < 0.0))
            {
                throw PayloadStorageError{MXL_ERR_INVALID_STATE, fmt::format("The payload descriptor at {} is incomplete.", path.string())};
            }
            return PayloadDescriptor{
                .backend = backend->second.get<std::string>(),
                .storageType = static_cast<std::uint32_t>(storageType->second.get<double>()),
            };
        }

        /**
         * Describe a discrete flow to a backend.
         * \param[in] info The flow info.
         * \param[in] slicesPerGrain The number of slices in a grain.
         * \param[in] mode The access this process needs.
         * \param[in] grainPayloadSize The size in bytes of the payload of one grain.
         * \return The flow description for the backend.
         */
        mxlPayloadBackendFlowInfo makeBackendFlowInfo(mxlFlowInfo const& info, std::size_t slicesPerGrain, AccessMode mode,
            std::uint64_t grainPayloadSize)
        {
            auto result = mxlPayloadBackendFlowInfo{};
            result.structSize = sizeof(mxlPayloadBackendFlowInfo);
            result.grainCount = info.config.discrete.grainCount;
            result.grainPayloadSize = grainPayloadSize;
            result.format = info.config.common.format;

            result.accessMode = (mode == AccessMode::READ_ONLY) ? MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY : MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE;
            result.slicesPerGrain = static_cast<std::uint32_t>(slicesPerGrain);
            for (auto const sliceSize : info.config.discrete.sliceSizes)
            {
                if (sliceSize == 0U)
                {
                    break;
                }
                result.sliceSizes[result.planeCount] = sliceSize;
                ++result.planeCount;
            }
            return result;
        }
    }

    FlowManager::FlowManager(std::filesystem::path const& in_mxlDomain)
        : FlowManager{in_mxlDomain, PayloadBackendLoader::defaultLoader()}
    {}

    FlowManager::FlowManager(std::filesystem::path const& in_mxlDomain, PayloadBackendLoader& backendLoader)
        : _mxlDomain{std::filesystem::canonical(in_mxlDomain)}
        , _backendLoader{&backendLoader}
    {
        if (!exists(in_mxlDomain) || !is_directory(in_mxlDomain))
        {
            throw std::filesystem::filesystem_error{
                "Path does not exist or is not a directory.", in_mxlDomain, std::make_error_code(std::errc::no_such_file_or_directory)};
        }
    }

    std::pair<bool, std::unique_ptr<DiscreteFlowData>> FlowManager::createOrOpenDiscreteFlow(uuids::uuid const& flowId, std::string const& flowDef,
        mxlDataFormat flowFormat, std::size_t grainCount, mxlRational const& grainRate, std::size_t grainPayloadSize, std::size_t grainNumOfSlices,
        std::array<uint32_t, MXL_MAX_PLANES_PER_GRAIN> grainSliceLengths, std::uint32_t maxSyncBatchSizeHintOpt,
        std::uint32_t maxCommitBatchSizeHintOpt)
    {
        return createOrOpenDiscreteFlowWithStorage(flowId,
            flowDef,
            flowFormat,
            grainCount,
            grainRate,
            grainPayloadSize,
            grainNumOfSlices,
            grainSliceLengths,
            maxSyncBatchSizeHintOpt,
            maxCommitBatchSizeHintOpt,
            PayloadStorageSpec{});
    }

    std::pair<bool, std::unique_ptr<DiscreteFlowData>> FlowManager::createOrOpenDiscreteFlowWithStorage(uuids::uuid const& flowId,
        std::string const& flowDef, mxlDataFormat flowFormat, std::size_t grainCount, mxlRational const& grainRate, std::size_t grainPayloadSize,
        std::size_t grainNumOfSlices, std::array<uint32_t, MXL_MAX_PLANES_PER_GRAIN> grainSliceLengths, std::uint32_t maxSyncBatchSizeHintOpt,
        std::uint32_t maxCommitBatchSizeHintOpt, PayloadStorageSpec const& storageSpec)
    {
        auto const uuidString = uuids::to_string(flowId);
        MXL_DEBUG("Create discrete flow. id: {}, grainCount: {}, grain payload size: {}, payload backend: {}",
            uuidString,
            grainCount,
            grainPayloadSize,
            storageSpec.usesBackend() ? storageSpec.backend : std::string{"built-in host"});

        flowFormat = sanitizeFlowFormat(flowFormat);
        if (!mxlIsDiscreteDataFormat(flowFormat))
        {
            throw std::runtime_error{"Attempt to create discrete flow with unsupported or non matching format."};
        }

        // Load the backend before touching the file system, so that a missing backend leaves nothing behind.
        mxlPayloadBackendApiV1 const* backendApi = nullptr;
        if (storageSpec.usesBackend())
        {
            try
            {
                backendApi = &_backendLoader->load(storageSpec.backend);
            }
            catch (std::exception const& e)
            {
                throw PayloadStorageError{MXL_ERR_UNSUPPORTED_OPERATION, e.what()};
            }
        }

        // With a backend, grain files hold only the grain header.
        auto const mappedPayloadSize = storageSpec.usesBackend() ? std::size_t{0} : grainPayloadSize;

        auto const tempDirectory = createTemporaryFlowDirectory(_mxlDomain);
        auto _ = defer(
            [&]() noexcept
            {
                std::error_code ec;
                std::filesystem::remove_all(tempDirectory, ec);
                if (ec)
                {
                    MXL_WARN("Failed to remove temporary flow directory: {}", ec.message());
                }
            });

        // Write the json file to disk.
        writeFlowDescriptor(tempDirectory, flowDef);

        // Create the dummy file.
        auto readAccessFile = makeFlowAccessFilePath(tempDirectory);
        if (auto out = std::ofstream{readAccessFile, std::ios::out | std::ios::trunc}; !out)
        {
            throw std::filesystem::filesystem_error{
                "Failed to create flow access file.", readAccessFile, std::make_error_code(std::errc::file_exists)};
        }

        auto const flowDataPath = makeFlowDataFilePath(tempDirectory);
        auto flowData = std::make_unique<DiscreteFlowData>(flowDataPath.string().c_str(), AccessMode::CREATE_READ_WRITE, LockMode::Shared);

        auto& info = *flowData->flowInfo();
        info.version = storageSpec.usesBackend() ? FLOW_DATA_VERSION_PAYLOAD_BACKEND : FLOW_DATA_VERSION;
        info.size = sizeof info;
        info.config.common = initCommonFlowConfigInfo(flowId, flowFormat, grainRate, maxSyncBatchSizeHintOpt, maxCommitBatchSizeHintOpt);
        info.config.discrete = {};
        info.config.discrete.grainCount = grainCount;
        std::copy(grainSliceLengths.begin(), grainSliceLengths.end(), info.config.discrete.sliceSizes);

        info.runtime = initFlowRuntimeInfo();

        auto& state = *flowData->flowState();
        state = initFlowState(flowDataPath);

        auto const grainDir = makeGrainDirectoryName(tempDirectory);
        if (!create_directory(grainDir))
        {
            throw std::filesystem::filesystem_error{"Could not create grain directory.", grainDir, std::make_error_code(std::errc::io_error)};
        }

        for (auto i = std::size_t{0}; i < grainCount; ++i)
        {
            auto const grainPath = makeGrainDataFilePath(grainDir, i);
            MXL_TRACE("Creating grain: {}", grainPath.string());

            auto const grain = flowData->emplaceGrain(grainPath.string().c_str(), mappedPayloadSize);
            auto& gInfo = grain->header.info;
            gInfo.grainSize = grainPayloadSize;
            gInfo.totalSlices = grainNumOfSlices;
            gInfo.validSlices = 0;
            gInfo.version = GRAIN_HEADER_VERSION;
            gInfo.size = sizeof gInfo;
        }

        auto const finalDir = makeFlowDirectoryName(_mxlDomain, uuidString);
        if (backendApi != nullptr)
        {
            // The backend allocates and writes its files while the flow is still in the temporary directory, so
            // that the flow is complete when it is published.
            writePayloadDescriptor(tempDirectory, storageSpec, backendApi->storageType);
            auto const backendInfo = makeBackendFlowInfo(info, grainNumOfSlices, AccessMode::READ_WRITE, grainPayloadSize);
            flowData->setPayloadStorage(PluginPayloadStorage::prepare(*backendApi, backendInfo, storageSpec.backendOptions, tempDirectory, finalDir));
            // The backend chose the device from its own options. Record it for readers and tools.
            info.config.common.deviceIndex = flowData->payloadStorage().deviceIndex();
        }
        else
        {
            flowData->setPayloadStorage(std::make_unique<HostPayloadStorage>(*flowData));
        }

        if (publishFlowDirectory(tempDirectory, finalDir))
        {
            return {true, std::move(flowData)};
        }

        // Another writer created the flow first. Release the storage prepared for it before opening the existing one.
        flowData.reset();
        auto existingFlowData = dynamic_pointer_cast<DiscreteFlowData>(openFlow(flowId, AccessMode::READ_WRITE));
        if (!existingFlowData)
        {
            throw std::runtime_error("Could not open existing flow because it is of a different format");
        }

        // A flow whose payload is held by a backend has exactly one writer. A writer that asks for a backend cannot
        // join a flow with built-in storage either.
        if (storageSpec.usesBackend() || (existingFlowData->flowInfo()->version != FLOW_DATA_VERSION))
        {
            throw PayloadStorageError{MXL_ERR_CONFLICT, "A flow whose payload is held by a backend accepts only one writer."};
        }

        return {false, std::move(existingFlowData)};
    }

    std::pair<bool, std::unique_ptr<ContinuousFlowData>> FlowManager::createOrOpenContinuousFlow(uuids::uuid const& flowId,
        std::string const& flowDef, mxlDataFormat flowFormat, mxlRational const& sampleRate, std::size_t channelCount, std::size_t sampleWordSize,
        std::size_t bufferLength, std::uint32_t maxSyncBatchSizeHintOpt, std::uint32_t maxCommitBatchSizeHintOpt)
    {
        auto const uuidString = uuids::to_string(flowId);
        MXL_DEBUG("Create continuous flow. id: {}, channel count: {}, word size: {}, buffer length: {}",
            uuidString,
            channelCount,
            sampleWordSize,
            bufferLength);

        flowFormat = sanitizeFlowFormat(flowFormat);
        if (!mxlIsContinuousDataFormat(flowFormat))
        {
            throw std::runtime_error{"Attempt to create continuous flow with unsupported or non matching format."};
        }

        auto const tempDirectory = createTemporaryFlowDirectory(_mxlDomain);
        try
        {
            // Write the json file to disk.
            writeFlowDescriptor(tempDirectory, flowDef);

            auto const flowDataPath = makeFlowDataFilePath(tempDirectory);
            auto flowData = std::make_unique<ContinuousFlowData>(flowDataPath.string().c_str(), AccessMode::CREATE_READ_WRITE, LockMode::Shared);

            auto& info = *flowData->flowInfo();
            info.version = FLOW_DATA_VERSION;
            info.size = sizeof info;
            info.config.common = initCommonFlowConfigInfo(flowId, flowFormat, sampleRate, maxSyncBatchSizeHintOpt, maxCommitBatchSizeHintOpt);
            info.config.continuous = {};
            info.config.continuous.channelCount = channelCount;
            info.config.continuous.bufferLength = bufferLength;

            info.runtime = initFlowRuntimeInfo();

            auto& state = *flowData->flowState();
            state = initFlowState(flowDataPath);

            flowData->openChannelBuffers(makeChannelDataFilePath(tempDirectory).string().c_str(), sampleWordSize);

            auto const finalDir = makeFlowDirectoryName(_mxlDomain, uuidString);
            if (publishFlowDirectory(tempDirectory, finalDir))
            {
                return {true, std::move(flowData)};
            }
            else
            {
                auto existingFlowData = dynamic_pointer_cast<ContinuousFlowData>(openFlow(flowId, AccessMode::READ_WRITE));
                if (!existingFlowData)
                {
                    throw std::runtime_error("Could not open existing flow because it is of a different format");
                }

                return {false, std::move(existingFlowData)};
            }
        }
        catch (...)
        {
            auto ec = std::error_code{};
            remove_all(tempDirectory, ec);
            throw;
        }
    }

    std::unique_ptr<FlowData> FlowManager::openFlow(uuids::uuid const& in_flowId, AccessMode in_mode)
    {
        return openFlowWithPayloadOptions(in_flowId, in_mode, std::string{});
    }

    std::unique_ptr<FlowData> FlowManager::openFlowWithPayloadOptions(uuids::uuid const& in_flowId, AccessMode in_mode,
        std::string const& in_payloadOptions)
    {
        if (in_mode == AccessMode::CREATE_READ_WRITE)
        {
            throw std::invalid_argument{"Attempt to open flow with invalid access mode."};
        }

        auto uuid = uuids::to_string(in_flowId);
        auto const base = makeFlowDirectoryName(_mxlDomain, uuid);

        // Verify that the flow file exists.
        if (auto const flowFile = makeFlowDataFilePath(base); exists(flowFile))
        {
            auto flowSegment = SharedMemoryInstance<Flow>{flowFile.string().c_str(), in_mode, 0U, LockMode::Shared};
            auto const version = flowSegment.get()->info.version;
            if ((version != FLOW_DATA_VERSION) && (version != FLOW_DATA_VERSION_PAYLOAD_BACKEND))
            {
                throw std::invalid_argument{fmt::format(
                    "Unsupported flow data version: {}, supported are: {} and {}", version, FLOW_DATA_VERSION, FLOW_DATA_VERSION_PAYLOAD_BACKEND)};
            }

            // MXL writers leave the deprecated payload location at 0. Another value comes from software that keeps the payload
            // somewhere this library cannot map.
            if (auto const payloadLocation = flowSegment.get()->info.config.common.payloadLocation; payloadLocation != 0U)
            {
                throw std::invalid_argument{fmt::format("Unsupported payload location: {}, supported is: 0", payloadLocation)};
            }

            if (auto const flowFormat = flowSegment.get()->info.config.common.format; mxlIsDiscreteDataFormat(flowFormat))
            {
                return openDiscreteFlow(base, std::move(flowSegment), in_payloadOptions);
            }
            else if (version != FLOW_DATA_VERSION)
            {
                throw std::invalid_argument{"Continuous flows only exist in flow data version 1."};
            }
            else if (mxlIsContinuousDataFormat(flowFormat))
            {
                return openContinuousFlow(base, std::move(flowSegment));
            }
            else
            {
                // This should never happen for a valid flow.
                throw std::runtime_error{"Attempt to open flow with unsupported data format."};
            }
        }
        else
        {
            throw std::filesystem::filesystem_error{"Flow file not found.", flowFile, std::make_error_code(std::errc::no_such_file_or_directory)};
        }
    }

    std::unique_ptr<DiscreteFlowData> FlowManager::openDiscreteFlow(std::filesystem::path const& flowDir,
        SharedMemoryInstance<Flow>&& sharedFlowInstance, std::string const& payloadOptions)
    {
        auto flowData = std::make_unique<DiscreteFlowData>(std::move(sharedFlowInstance));

        auto const grainCount = flowData->flowInfo()->config.discrete.grainCount;
        if (grainCount > 0U)
        {
            auto const grainDir = makeGrainDirectoryName(flowDir);
            if (exists(grainDir) && is_directory(grainDir))
            {
                // Open each grain with per-item error handling
                for (auto i = 0U; i < grainCount; ++i)
                {
                    auto const grainPath = makeGrainDataFilePath(grainDir, i).string();
                    MXL_TRACE("Opening grain: {}", grainPath);

                    flowData->emplaceGrain(grainPath.c_str(), /*payloadSize=*/0U);
                }
            }
            else
            {
                throw std::filesystem::filesystem_error{
                    "Grain directory not found.", grainDir, std::make_error_code(std::errc::no_such_file_or_directory)};
            }
        }

        if (flowData->flowInfo()->version == FLOW_DATA_VERSION)
        {
            flowData->setPayloadStorage(std::make_unique<HostPayloadStorage>(*flowData));
            return flowData;
        }

        // The payload is held by a backend. Opening must not require the backend's device, so a backend that cannot
        // be loaded here leaves the flow readable for its metadata, and payload access reports why.
        auto const descriptor = readPayloadDescriptor(flowDir);
        auto const grainPayloadSize = (grainCount > 0U) ? flowData->grainInfoAt(0)->grainSize : 0U;
        auto const slicesPerGrain = (grainCount > 0U) ? flowData->grainInfoAt(0)->totalSlices : 0U;
        auto const backendInfo = makeBackendFlowInfo(*flowData->flowInfo(), slicesPerGrain, flowData->accessMode(), grainPayloadSize);
        mxlPayloadBackendApiV1 const* api = nullptr;
        try
        {
            api = &_backendLoader->load(descriptor.backend);
        }
        catch (std::exception const& e)
        {
            MXL_WARN("The payload of flow {} cannot be accessed in this process: {}", flowDir.string(), e.what());
            flowData->setPayloadStorage(std::make_unique<UnavailablePayloadStorage>(
                static_cast<mxlPayloadStorageType>(descriptor.storageType), grainCount, MXL_ERR_UNSUPPORTED_OPERATION, e.what()));
            return flowData;
        }

        // Invalid reader options are the caller's error, so a failure to open is reported.
        flowData->setPayloadStorage(PluginPayloadStorage::open(*api, backendInfo, payloadOptions, flowDir));

        return flowData;
    }

    std::unique_ptr<ContinuousFlowData> FlowManager::openContinuousFlow(std::filesystem::path const& flowDir,
        SharedMemoryInstance<Flow>&& sharedFlowInstance)
    {
        auto flowData = std::make_unique<ContinuousFlowData>(std::move(sharedFlowInstance));

        flowData->openChannelBuffers(makeChannelDataFilePath(flowDir).string().c_str(), /*payloadSize=*/0U);

        return flowData;
    }

    bool FlowManager::deleteFlow(std::unique_ptr<FlowData>&& flowData)
    {
        if (flowData)
        {
            // Extract the ID
            auto const span = uuids::span<std::uint8_t, sizeof flowData->flowInfo()->config.common.id>{
                const_cast<std::uint8_t*>(flowData->flowInfo()->config.common.id), sizeof flowData->flowInfo()->config.common.id};
            auto const id = uuids::uuid(span);

            // Close the flow
            flowData.reset();

            // Delegate to the other deleteFlow overload
            return deleteFlow(id);
        }
        return false;
    }

    bool FlowManager::deleteFlow(uuids::uuid const& flowId)
    {
        auto uuid = uuids::to_string(flowId);
        MXL_TRACE("Delete flow: {}", uuid);

        try
        {
            // Compute the flow directory path
            auto const flowPath = makeFlowDirectoryName(_mxlDomain, uuid);
            auto const removed = remove_all(flowPath);
            if (removed == 0)
            {
                MXL_TRACE("Flow not found or already deleted: {}", uuid);
                return false;
            }
            return true;
        }
        catch (...)
        {
            // Convert any filesystem exception to false return
            // This makes the method effectively noexcept while indicating failure
            return false;
        }
    }

    std::vector<uuids::uuid> FlowManager::listFlows() const
    {
        auto base = std::filesystem::path{_mxlDomain};

        auto flowIds = std::vector<uuids::uuid>{};
        if (exists(base) && is_directory(base))
        {
            for (auto const& entry : std::filesystem::directory_iterator{_mxlDomain})
            {
                if (is_directory(entry) && (entry.path().extension() == FLOW_DIRECTORY_NAME_SUFFIX))
                {
                    // this looks like a uuid. try to parse it an confirm it is valid.
                    auto id = uuids::uuid::from_string(entry.path().stem().string());
                    if (id.has_value())
                    {
                        flowIds.push_back(*id);
                    }
                }
            }
        }
        else
        {
            throw std::filesystem::filesystem_error{"Base directory not found.", base, std::make_error_code(std::errc::no_such_file_or_directory)};
        }

        return flowIds;
    }

    std::string FlowManager::getFlowDef(uuids::uuid const& flowId) const
    {
        auto const uuid = uuids::to_string(flowId);
        auto const flowPath = makeFlowDirectoryName(_mxlDomain, uuid);
        auto const flowJsonFile = makeFlowDescriptorFilePath(flowPath);
        if (auto in = std::ifstream{flowJsonFile, std::ios::in}; in)
        {
            auto const result = std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
            if (!in)
            {
                throw std::runtime_error{"Error while reading the flow definition."};
            }
            return result;
        }
        // Here is a race condition, but plain C++ API does not provide a way to check whether it was not possible to open a file because it does
        // not exist, or whether the access rights are wrong.
        if (!exists(flowJsonFile))
        {
            throw std::filesystem::filesystem_error{"Failed to open flow resource definition - file not found.",
                flowJsonFile,
                std::make_error_code(std::errc::no_such_file_or_directory)};
        }
        throw std::runtime_error{"Failed to open flow resource definition."};
    }

    std::filesystem::path const& FlowManager::getDomain() const
    {
        return _mxlDomain;
    }
}
