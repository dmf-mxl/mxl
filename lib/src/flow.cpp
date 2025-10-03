// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl/flow.h"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <uuid.h>
#include <sys/file.h>
#include "internal/Instance.hpp"
#include "internal/Logging.hpp"
#include "internal/PathUtils.hpp"
#include "mxl/dataformat.h"
#include "mxl/mxl.h"

using namespace mxl::lib;

extern "C"
MXL_EXPORT
mxlStatus mxlCreateFlow(mxlInstance instance, char const* flowDef, char const* /*options*/, mxlFlowInfo* flowInfo)
{
    try
    {
        if (flowDef != nullptr)
        {
            if (auto const cppInstance = to_Instance(instance); cppInstance != nullptr)
            {
                auto const flowData = cppInstance->createFlow(flowDef);
                *flowInfo = *flowData->flowInfo();
                return MXL_STATUS_OK;
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (std::filesystem::filesystem_error const& e)
    {
        MXL_ERROR("Failed to create flow : {}", e.what());
        auto const code = e.code();
        if ((code == std::errc::permission_denied) || (code == std::errc::operation_not_permitted) || (code == std::errc::read_only_file_system))
        {
            MXL_ERROR("Filesystem permission/access error: {}", code.message());
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
    }
    catch (...)
    {
        MXL_ERROR("Failed to create flow : {}", "An unknown error occured.");
    }
    return MXL_ERR_UNKNOWN;
}

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
mxlStatus mxlDestroyFlow(mxlInstance instance, char const* flowId)
{
    try
    {
        if (auto const cppInstance = to_Instance(instance); cppInstance != nullptr)
        {
            if (flowId != nullptr)
            {
                auto const id = uuids::uuid::from_string(flowId);
                if (id.has_value())
                {
                    auto const found = cppInstance->deleteFlow(*id);
                    return (found) ? MXL_STATUS_OK : MXL_ERR_FLOW_NOT_FOUND;
                }
            }
        }

        return MXL_ERR_INVALID_ARG;
    }
    catch (std::exception const& e)
    {
        MXL_ERROR("Failed to destroy flow : {}", e.what());
    }
    catch (...)
    {
        MXL_ERROR("Failed to destroy flow : {}", "An unknown error occured.");
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
mxlStatus mxlCreateFlowWriter(mxlInstance instance, char const* flowId, char const* /*options */, mxlFlowWriter* writer)
{
    try
    {
        if (writer != nullptr)
        {
            if (auto const cppInstance = to_Instance(instance); cppInstance != nullptr)
            {
                if ((flowId != nullptr) && uuids::uuid::is_valid_uuid(flowId))
                {
                    *writer = reinterpret_cast<mxlFlowWriter>(cppInstance->getFlowWriter(flowId));
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
mxlStatus mxlReleaseFlowWriter(mxlInstance instance, mxlFlowWriter writer)
{
    try
    {
        auto const cppInstance = to_Instance(instance);
        auto const cppWriter = to_FlowWriter(writer);
        if ((cppInstance != nullptr) && (cppWriter != nullptr))
        {
            cppInstance->releaseWriter(cppWriter);
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
mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, mxlGrainInfo* grainInfo, uint8_t** payload)
{
    try
    {
        if ((grainInfo != nullptr) && (payload != nullptr))
        {
            if (auto const cppReader = dynamic_cast<DiscreteFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                return cppReader->getGrain(index, timeoutNs, grainInfo, payload);
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
    try
    {
        if ((grainInfo != nullptr) && (payload != nullptr))
        {
            if (auto const cppReader = dynamic_cast<DiscreteFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                return cppReader->getGrain(index, grainInfo, payload);
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
mxlStatus mxlFlowReaderGetSamples(mxlFlowReader reader, uint64_t index, size_t count, mxlWrappedMultiBufferSlice* payloadBuffersSlices)
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
