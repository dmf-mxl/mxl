// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl/flow.h"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <uuid.h>
#include <sys/file.h>
#include <mxl/mxl.h>
#include "mxl-internal/Instance.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

namespace
{
    mxl::lib::Timepoint toDeadline(std::uint64_t timeoutNs) noexcept
    {
        return currentTime(mxl::lib::Clock::Realtime) + mxl::lib::Duration{static_cast<std::int64_t>(timeoutNs)};
    }
}

using namespace mxl::lib;

extern "C"
MXL_EXPORT
mxlStatus mxlIsFlowActive(mxlInstance instance, char const* flowId, bool* isActive)
{
    try
    {
        if (isActive != nullptr)
        {
            if (auto const cppInstance = to_Instance(instance); cppInstance != nullptr)
            {
                if ((flowId != nullptr) && uuids::uuid::is_valid_uuid(flowId))
                {
                    auto const id = uuids::uuid::from_string(flowId);
                    if (id.has_value())
                    {
                        auto const domain = cppInstance->getDomain();

                        // Try to obtain an exclusive lock on the flow data file.  If we can obtain one it means that no
                        // other process is writing to the flow.
                        auto flowDataFile = mxl::lib::makeFlowDataFilePath(domain, flowId);

                        int fd = open(flowDataFile.c_str(), O_RDONLY | O_CLOEXEC);
                        if (fd < 0)
                        {
                            MXL_ERROR("Failed to open flow data file {} : {}", flowDataFile.string(), std::strerror(errno));
                            return MXL_ERR_FLOW_NOT_FOUND;
                        }

                        // Try to obtain an exclusive lock on the file descriptor. Do not block if the lock cannot be obtained.
                        bool active = flock(fd, LOCK_EX | LOCK_NB) < 0;
                        close(fd);

                        *isActive = active;
                        return MXL_STATUS_OK;
                    }
                }
            }
            return MXL_ERR_INVALID_ARG;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (std::exception const& e)
    {
        MXL_ERROR("Failed to check if flow is active : {}", e.what());
    }
    catch (...)
    {
        MXL_ERROR("Failed to check if flow is active : {}", "An unknown error occured.");
    }
    return MXL_ERR_UNKNOWN;
}

extern "C"
MXL_EXPORT
mxlStatus mxlGetFlowDef(mxlInstance instance, char const* flowId, char* buffer, size_t* bufferSize)
{
    if (flowId == nullptr || bufferSize == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    try
    {
        if (auto const cppInstance = to_Instance(instance); cppInstance != nullptr)
        {
            if (auto const id = uuids::uuid::from_string(flowId); id.has_value())
            {
                auto const flowDef = cppInstance->getFlowDef(*id);
                auto const requiredSize = flowDef.size() + 1; // +1 for the null terminator
                if (buffer == nullptr || *bufferSize < requiredSize)
                {
                    *bufferSize = requiredSize;
                    return MXL_ERR_INVALID_ARG;
                }
                *bufferSize = requiredSize;
                std::strncpy(buffer, flowDef.c_str(), requiredSize);
                return MXL_STATUS_OK;
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (std::filesystem::filesystem_error const& e)
    {
        MXL_ERROR("Failed to get flow definition : {}", e.what());
        return MXL_ERR_FLOW_NOT_FOUND;
    }
    catch (std::exception const& e)
    {
        MXL_ERROR("Failed to get flow definition : {}", e.what());
    }
    catch (...)
    {
        MXL_ERROR("Failed to get flow definition : {}", "An unknown error occured.");
    }
    return MXL_ERR_UNKNOWN;
}

extern "C"
MXL_EXPORT
mxlStatus mxlCreateFlowReader(mxlInstance instance, char const* flowId, char const* /*options*/, mxlFlowReader* reader)
{
    try
    {
        if (reader != nullptr)
        {
            if (auto const cppInstance = to_Instance(instance); cppInstance != nullptr)
            {
                if ((flowId != nullptr) && uuids::uuid::is_valid_uuid(flowId))
                {
                    *reader = reinterpret_cast<mxlFlowReader>(cppInstance->getFlowReader(flowId));
                    return MXL_STATUS_OK;
                }
            }
        }

        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlReleaseFlowReader(mxlInstance instance, mxlFlowReader reader)
{
    try
    {
        auto const cppInstance = to_Instance(instance);
        auto const cppReader = to_FlowReader(reader);
        if ((cppInstance != nullptr) && (cppReader != nullptr))
        {
            cppInstance->releaseReader(cppReader);
            return MXL_STATUS_OK;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlCreateFlowWriter(mxlInstance instance, char const* flowDef, char const* options, mxlFlowWriter* writer, mxlFlowConfigInfo* configInfo,
    bool* created)
{
    if ((instance == nullptr) || (flowDef == nullptr) || (writer == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    auto flowData = std::unique_ptr<FlowData>{};
    auto cppInstance = to_Instance(instance);

    if (cppInstance == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    try
    {
        static_assert(std::is_trivially_copy_assignable_v<mxlFlowConfigInfo>, "The type mxlFlowConfigInfo must be trivially copy assignable.");
        static_assert(std::is_nothrow_copy_assignable_v<mxlFlowConfigInfo>, "Copying mxlFlowConfigInfo cannot throw.");

        auto [flowConfigInfo, flowWriter, flowCreated] = cppInstance->createFlowWriter(flowDef,
            (options == nullptr) ? std::nullopt : std::make_optional(options));

        *writer = reinterpret_cast<mxlFlowWriter>(flowWriter);

        if (configInfo != nullptr)
        {
            *configInfo = flowConfigInfo;
        }
        if (created != nullptr)
        {
            *created = flowCreated;
        }
    }
    catch (std::filesystem::filesystem_error const& e)
    {
        MXL_ERROR("Failed to create flow writer: {}", e.what());
        auto const code = e.code();
        if ((code == std::errc::permission_denied) || (code == std::errc::operation_not_permitted) || (code == std::errc::read_only_file_system))
        {
            return MXL_ERR_PERMISSION_DENIED;
        }
        else
        {
            MXL_ERROR("Filesystem error: {}", code.message());
            return MXL_ERR_UNKNOWN;
        }
    }
    catch (std::exception const& e)
    {
        MXL_ERROR("Failed to create flow : {}", e.what());
        return MXL_ERR_UNKNOWN;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }

    return MXL_STATUS_OK;
}

extern "C"
MXL_EXPORT
mxlStatus mxlReleaseFlowWriter(mxlInstance instance, mxlFlowWriter writer)
{
    if ((instance == nullptr) || (writer == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    auto err = MXL_STATUS_OK;
    auto const cppInstance = to_Instance(instance);
    auto const cppWriter = to_FlowWriter(writer);

    if ((cppInstance == nullptr) || (cppWriter == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    try
    {
        cppInstance->releaseWriter(cppWriter);
    }
    catch (std::exception const& ex)
    {
        MXL_ERROR("Failed to release writer: {}", ex.what());
        return MXL_ERR_UNKNOWN;
    }

    return err;
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetInfo(mxlFlowReader reader, mxlFlowInfo* info)
{
    try
    {
        if (info != nullptr)
        {
            if (auto const cppReader = to_FlowReader(reader); cppReader != nullptr)
            {
                *info = cppReader->getFlowInfo();
                return MXL_STATUS_OK;
            }
            return MXL_ERR_INVALID_FLOW_READER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetConfigInfo(mxlFlowReader reader, mxlFlowConfigInfo* info)
{
    try
    {
        if (info != nullptr)
        {
            if (auto const cppReader = to_FlowReader(reader); cppReader != nullptr)
            {
                *info = cppReader->getFlowInfo().config;
                return MXL_STATUS_OK;
            }
            return MXL_ERR_INVALID_FLOW_READER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetRuntimeInfo(mxlFlowReader reader, mxlFlowRuntimeInfo* info)
{
    try
    {
        if (info != nullptr)
        {
            if (auto const cppReader = to_FlowReader(reader); cppReader != nullptr)
            {
                *info = cppReader->getFlowInfo().runtime;
                return MXL_STATUS_OK;
            }
            return MXL_ERR_INVALID_FLOW_READER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, mxlGrainInfo* grainInfo, uint8_t** payload)
{
    return mxlFlowReaderGetGrainSlice(reader, index, UINT16_MAX, timeoutNs, grainInfo, payload);
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetGrainSlice(mxlFlowReader reader, uint64_t index, uint16_t minValidSlices, uint64_t timeoutNs, mxlGrainInfo* grainInfo,
    uint8_t** payload)
{
    try
    {
        if ((grainInfo != nullptr) && (payload != nullptr))
        {
            if (auto const cppReader = dynamic_cast<DiscreteFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                return cppReader->getGrain(index, minValidSlices, toDeadline(timeoutNs), grainInfo, payload);
            }
            return MXL_ERR_INVALID_FLOW_READER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader reader, uint64_t index, mxlGrainInfo* grainInfo, uint8_t** payload)
{
    return mxlFlowReaderGetGrainSliceNonBlocking(reader, index, UINT16_MAX, grainInfo, payload);
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetGrainSliceNonBlocking(mxlFlowReader reader, uint64_t index, uint16_t minValidSlices, mxlGrainInfo* grainInfo,
    uint8_t** payload)
{
    try
    {
        if ((grainInfo != nullptr) && (payload != nullptr))
        {
            if (auto const cppReader = dynamic_cast<DiscreteFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                return cppReader->getGrain(index, minValidSlices, grainInfo, payload);
            }
            return MXL_ERR_INVALID_FLOW_READER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterGetGrainInfo(mxlFlowWriter writer, uint64_t index, mxlGrainInfo* grainInfo)
{
    try
    {
        if (grainInfo != nullptr)
        {
            if (auto const cppWriter = dynamic_cast<DiscreteFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
            {
                *grainInfo = cppWriter->getGrainInfo(index);
                return MXL_STATUS_OK;
            }

            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter writer, uint64_t index, mxlGrainInfo* grainInfo, uint8_t** payload)
{
    try
    {
        if ((grainInfo != nullptr) && (payload != nullptr))
        {
            if (auto const cppWriter = dynamic_cast<DiscreteFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
            {
                return cppWriter->openGrain(index, grainInfo, payload);
            }

            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterCancelGrain(mxlFlowWriter writer)
{
    try
    {
        if (auto const cppWriter = dynamic_cast<DiscreteFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
        {
            return cppWriter->cancel();
        }
        return MXL_ERR_INVALID_FLOW_WRITER;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterCommitGrain(mxlFlowWriter writer, mxlGrainInfo const* grainInfo)
{
    if (grainInfo == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    try
    {
        if (auto const cppWriter = dynamic_cast<DiscreteFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
        {
            return cppWriter->commit(*grainInfo);
        }
        return MXL_ERR_INVALID_FLOW_WRITER;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetSamples(mxlFlowReader reader, uint64_t index, size_t count, uint64_t timeoutNs,
    mxlWrappedMultiBufferSlice* payloadBuffersSlices)
{
    try
    {
        if (payloadBuffersSlices != nullptr)
        {
            if (auto const cppReader = dynamic_cast<ContinuousFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                return cppReader->getSamples(index, count, toDeadline(timeoutNs), *payloadBuffersSlices);
            }

            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowReaderGetSamplesNonBlocking(mxlFlowReader reader, uint64_t index, size_t count, mxlWrappedMultiBufferSlice* payloadBuffersSlices)
{
    try
    {
        if (payloadBuffersSlices != nullptr)
        {
            if (auto const cppReader = dynamic_cast<ContinuousFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                return cppReader->getSamples(index, count, *payloadBuffersSlices);
            }

            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterOpenSamples(mxlFlowWriter writer, uint64_t index, size_t count, mxlMutableWrappedMultiBufferSlice* payloadBuffersSlices)
{
    try
    {
        if (payloadBuffersSlices != nullptr)
        {
            if (auto const cppWriter = dynamic_cast<ContinuousFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
            {
                return cppWriter->openSamples(index, count, *payloadBuffersSlices);
            }

            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterCancelSamples(mxlFlowWriter writer)
{
    try
    {
        if (auto const cppWriter = dynamic_cast<ContinuousFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
        {
            return cppWriter->cancel();
        }
        return MXL_ERR_INVALID_FLOW_WRITER;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlFlowWriterCommitSamples(mxlFlowWriter writer)
{
    try
    {
        if (auto const cppWriter = dynamic_cast<ContinuousFlowWriter*>(to_FlowWriter(writer)); cppWriter != nullptr)
        {
            return cppWriter->commit();
        }
        return MXL_ERR_INVALID_FLOW_WRITER;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}
