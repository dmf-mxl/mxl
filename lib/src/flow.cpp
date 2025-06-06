#include "mxl/flow.h"
#include <cstdint>
#include <exception>
#include <string>
#include <uuid.h>
#include "internal/Instance.hpp"
#include "internal/Logging.hpp"

using namespace mxl::lib;

extern "C"
MXL_EXPORT
mxlStatus mxlCreateFlow(mxlInstance instance, char const* flowDef, char const* /*options*/, FlowInfo* flowInfo)
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
mxlStatus mxlFlowReaderGetInfo(mxlFlowReader reader, FlowInfo* info)
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
mxlStatus mxlFlowReaderGetGrain(mxlFlowReader reader, uint64_t index, uint64_t timeoutNs, GrainInfo* grainInfo,
    uint8_t** payload)
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
mxlStatus mxlFlowReaderGetGrainNonBlocking(mxlFlowReader reader, uint64_t index, GrainInfo* grainInfo,
    uint8_t** payload)
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
mxlStatus mxlFlowWriterOpenGrain(mxlFlowWriter writer, uint64_t index, GrainInfo* grainInfo, uint8_t** payload)
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
mxlStatus mxlFlowWriterCommitGrain(mxlFlowWriter writer, GrainInfo const* grainInfo)
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
