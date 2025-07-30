// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "FlowManager.hpp"
#include <cstring>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <unistd.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <system_error>
#include "Logging.hpp"
#include "PathUtils.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    namespace
    {
        /**
         * Attempt to create a temporary directory to prepare a new flow.
         * The temporary name is structured in a way that prevents is from
         * clashing with directory names that belkong to established flows.
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

        void publishFlowDirectory(std::filesystem::path const& source, std::filesystem::path const& dest)
        {
            permissions(source,
                std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                    std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add);
            rename(source, dest);
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
                MXL_ERROR("Failed to create flow resource definition file '{}'", flowJsonFile.string());
                throw std::filesystem::filesystem_error{
                    "Failed to create flow resource definition.", flowJsonFile, std::make_error_code(std::errc::io_error)};
            }
        }

        CommonFlowInfo initCommonFlowInfo(uuids::uuid const& flowId, mxlDataFormat format)
        {
            auto result = CommonFlowInfo{};

            auto const idSpan = flowId.as_bytes();
            std::memcpy(result.id, idSpan.data(), idSpan.size());
            result.lastWriteTime = mxlGetTime();
            result.lastReadTime = result.lastWriteTime;
            result.format = format;

            return result;
        }

    }

    FlowManager::FlowManager(std::filesystem::path const& in_mxlDomain)
        : _mxlDomain{std::filesystem::canonical(in_mxlDomain)}
    {
        if (!exists(in_mxlDomain) || !is_directory(in_mxlDomain))
        {
            MXL_ERROR("Domain path '{}' does not exist or is not a directory", in_mxlDomain.string());
            throw std::filesystem::filesystem_error{
                "Path does not exist or is not a directory.", in_mxlDomain, std::make_error_code(std::errc::no_such_file_or_directory)};
        }
    }

    std::unique_ptr<DiscreteFlowData> FlowManager::createDiscreteFlow(uuids::uuid const& flowId, std::string const& flowDef, mxlDataFormat flowFormat,
        std::size_t grainCount, Rational const& grainRate, std::size_t grainPayloadSize)
    {
        auto const uuidString = uuids::to_string(flowId);
        MXL_DEBUG("Create discrete flow. id: {}, grainCount: {}, grain payload size: {}", uuidString, grainCount, grainPayloadSize);

        flowFormat = sanitizeFlowFormat(flowFormat);
        if (!mxlIsDiscreteDataFormat(flowFormat))
        {
            throw std::runtime_error{"Attempt to create discrete flow with unsupported or non matching format."};
        }

        auto const tempDirectory = createTemporaryFlowDirectory(_mxlDomain);
        try
        {
            // Write the json file to disk.
            writeFlowDescriptor(tempDirectory, flowDef);

            // Create the dummy file.
            auto readAccessFile = makeFlowAccessFilePath(tempDirectory);
            if (auto out = std::ofstream{readAccessFile, std::ios::out | std::ios::trunc}; !out)
            {
                MXL_ERROR("Failed to create flow access file '{}'", readAccessFile.string());
                throw std::filesystem::filesystem_error{
                    "Failed to create flow access file.", readAccessFile, std::make_error_code(std::errc::file_exists)};
            }

            auto flowData = std::make_unique<DiscreteFlowData>(makeFlowDataFilePath(tempDirectory).string().c_str(), AccessMode::CREATE_READ_WRITE);

            auto& info = *flowData->flowInfo();
            info.version = 1;
            info.size = sizeof info;

            info.common = initCommonFlowInfo(flowId, flowFormat);

            info.discrete.grainRate = grainRate;
            info.discrete.grainCount = grainCount;
            info.discrete.syncCounter = 0;

            auto const grainDir = makeGrainDirectoryName(tempDirectory);
            if (!std::filesystem::create_directory(grainDir))
            {
                MXL_ERROR("Could not create grain directory '{}'", grainDir.string());
                throw std::filesystem::filesystem_error{"Could not create grain directory.", grainDir, std::make_error_code(std::errc::io_error)};
            }

            for (auto i = std::size_t{0}; i < grainCount; ++i)
            {
                auto const grainPath = makeGrainDataFilePath(grainDir, i);
                MXL_TRACE("Creating grain: {}", grainPath.string());

                // \todo Handle payload stored device memory
                auto const grain = flowData->emplaceGrain(grainPath.string().c_str(), grainPayloadSize);
                auto& gInfo = grain->header.info;
                gInfo.grainSize = grainPayloadSize;
                gInfo.version = 1;
                gInfo.size = sizeof gInfo;
                gInfo.deviceIndex = -1;
            }

            auto const finalDir = makeFlowDirectoryName(_mxlDomain, uuidString);
            publishFlowDirectory(tempDirectory, finalDir);

            return flowData;
        }
        catch (...)
        {
            auto ec = std::error_code{};
            remove_all(tempDirectory, ec);
            throw;
        }
    }

    std::unique_ptr<ContinuousFlowData> FlowManager::createContinuousFlow(uuids::uuid const& flowId, std::string const& flowDef,
        mxlDataFormat flowFormat, Rational const& sampleRate, std::size_t channelCount, std::size_t sampleWordSize, std::size_t bufferLength)
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

            auto flowData = std::make_unique<ContinuousFlowData>(makeFlowDataFilePath(tempDirectory).string().c_str(), AccessMode::CREATE_READ_WRITE);

            auto& info = *flowData->flowInfo();
            info.version = 1;
            info.size = sizeof info;

            info.common = initCommonFlowInfo(flowId, flowFormat);

            info.continuous = {};
            info.continuous.sampleRate = sampleRate;
            info.continuous.channelCount = channelCount;
            info.continuous.bufferLength = bufferLength;

            flowData->openChannelBuffers(makeChannelDataFilePath(tempDirectory).string().c_str(), sampleWordSize);

            auto const finalDir = makeFlowDirectoryName(_mxlDomain, uuidString);
            publishFlowDirectory(tempDirectory, finalDir);

            return flowData;
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
        if (in_mode == AccessMode::CREATE_READ_WRITE)
        {
            throw std::invalid_argument{"Attempt to open flow with invalid access mode."};
        }

        auto uuid = uuids::to_string(in_flowId);
        auto const base = makeFlowDirectoryName(_mxlDomain, uuid);

        // Verify that the flow file exists.
        if (auto const flowFile = makeFlowDataFilePath(base); exists(flowFile))
        {
            auto flowSegment = SharedMemoryInstance<Flow>{flowFile.string().c_str(), in_mode, 0U};

            auto const* flow = flowSegment.get();
            auto const flowFormat = flow->info.common.format;
            if (mxlIsDiscreteDataFormat(flowFormat))
            {
                return openDiscreteFlow(base, std::move(flowSegment));
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
        SharedMemoryInstance<Flow>&& sharedFlowInstance)
    {
        auto flowData = std::make_unique<DiscreteFlowData>(std::move(sharedFlowInstance));

        auto const grainCount = flowData->flowInfo()->discrete.grainCount;
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

        return flowData;
    }

    std::unique_ptr<ContinuousFlowData> FlowManager::openContinuousFlow(std::filesystem::path const& flowDir,
        SharedMemoryInstance<Flow>&& sharedFlowInstance)
    {
        auto flowData = std::make_unique<ContinuousFlowData>(std::move(sharedFlowInstance));

        // Verify that the channel‐buffers file actually exists before attempting to open it
        auto const channelPath = makeChannelDataFilePath(flowDir);
        if (!std::filesystem::exists(channelPath))
        {
            throw std::filesystem::filesystem_error{
                "Channel buffer file not found.", channelPath, std::make_error_code(std::errc::no_such_file_or_directory)};
        }

        // Open the channel buffers (may throw on I/O or mmap failure)
        flowData->openChannelBuffers(channelPath.string().c_str(), /*payloadSize=*/0U);

        return flowData;
    }

    bool FlowManager::deleteFlow(std::unique_ptr<FlowData>&& flowData)
    {
        if (flowData)
        {
            // Extract the ID
            auto const span = uuids::span<std::uint8_t, sizeof flowData->flowInfo()->common.id>{
                const_cast<std::uint8_t*>(flowData->flowInfo()->common.id), sizeof flowData->flowInfo()->common.id};
            auto const id = uuids::uuid(span);

            // Close the flow
            flowData.reset();

            // Attempt filesystem deletion, catching any errors
            try
            {
                return deleteFlow(id);
            }
            catch (std::exception const& e)
            {
                MXL_ERROR("Failed to delete flow {}: {}", uuids::to_string(id), e.what());
                return false;
            }
            catch (...)
            {
                MXL_ERROR("Failed to delete flow {}: unknown error", uuids::to_string(id));
                return false;
            }
        }
        return false;
    }

    bool FlowManager::deleteFlow(uuids::uuid const& flowId)
    {
        auto uuid = uuids::to_string(flowId);
        MXL_TRACE("Delete flow: {}", uuid);

        // Compute the flow directory path
        auto const flowPath = makeFlowDirectoryName(_mxlDomain, uuid);
        try
        {
            auto const removed = std::filesystem::remove_all(flowPath);
            if (removed == 0)
            {
                MXL_TRACE("Flow not found or already deleted: {}", uuid);
                return false;
            }
            return true;
        }
        catch (std::exception const& e)
        {
            MXL_ERROR("Error deleting flow {} at {}: {}", uuid, flowPath.string(), e.what());
            return false;
        }
        catch (...)
        {
            MXL_ERROR("Error deleting flow {} at {}: unknown error", uuid, flowPath.string());
            return false;
        }
    }

    void FlowManager::garbageCollect()
    {
        MXL_WARN("Garbage collection of flows not implemented yet.");
        /// \todo Implement a garbage collection low priority thread.
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

    std::filesystem::path const& FlowManager::getDomain() const
    {
        return _mxlDomain;
    }
}