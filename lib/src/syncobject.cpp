// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl/syncobject.h"
#include "mxl/time.h"
#include "internal/Instance.hpp"
#include "internal/Thread.hpp"
#include "internal/Timing.hpp"

using namespace mxl::lib;

extern "C"
MXL_EXPORT
mxlStatus mxlSyncObjectInitWithTickRate(mxlRational const* rate, mxlSyncObject* syncObject)
{
    if ((rate != nullptr) && (rate->numerator != 0U) && (rate->denominator != 0U) && (syncObject != nullptr))
    {
        syncObject->tickRate = *rate;
        return MXL_STATUS_OK;
    }
    return MXL_ERR_INVALID_ARG;
}

extern "C"
MXL_EXPORT
mxlStatus mxlSyncObjectInitFromDiscreteFlow(mxlFlowReader reader, mxlSyncObject* syncObject)
{
    try
    {
        if (syncObject != nullptr)
        {
            if (auto const cppReader = dynamic_cast<DiscreteFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                auto const flowInfo = cppReader->getFlowInfo();
                auto const result = mxlSyncObjectInitWithTickRate(&flowInfo.discrete.grainRate, syncObject);
                if (result == MXL_STATUS_OK)
                {
                    syncObject->sourceDelay = flowInfo.common.sourceDelay;
                }
                return result;
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
mxlStatus mxlSyncObjectInitFromContinuousFLow(mxlFlowReader reader, int64_t batchSize, mxlSyncObject* syncObject)
{
    try
    {
        if ((syncObject != nullptr) && (batchSize > 0))
        {
            if (auto const cppReader = dynamic_cast<ContinuousFlowReader*>(to_FlowReader(reader)); cppReader != nullptr)
            {
                auto const flowInfo = cppReader->getFlowInfo();
                auto const result = mxlSyncObjectInitWithTickRate(&flowInfo.continuous.sampleRate, syncObject);
                if (result == MXL_STATUS_OK)
                {
                    syncObject->sourceDelay = flowInfo.common.sourceDelay;
                    if ((syncObject->tickRate.denominator % batchSize) == 0)
                    {
                        syncObject->tickRate.denominator /= batchSize;
                    }
                    else
                    {
                        syncObject->tickRate.numerator *= batchSize;
                    }
                }
                return result;
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
mxlStatus mxlSyncObjectAddFlow(mxlFlowReader reader, mxlSyncObject* syncObject)
{
    try
    {
        if (syncObject != nullptr)
        {
            if (auto const cppReader = to_FlowReader(reader); cppReader != nullptr)
            {
                auto const info = cppReader->getFlowInfo();
                syncObject->sourceDelay = (syncObject->sourceDelay >= info.common.sourceDelay)
                    ? syncObject->sourceDelay
                    : info.common.sourceDelay;
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
mxlStatus mxlSyncObjectWaitFor(mxlSyncObject const* syncObject, uint64_t tickIndex)
{
    if (syncObject != nullptr)
    {
        auto const timeStamp = mxlIndexToTimestamp(&syncObject->tickRate, tickIndex) + syncObject->sourceDelay;
        auto const result = this_thread::sleepUntil(Timepoint(timeStamp), Clock::TAI);
        if (result == 0)
        {
            return MXL_STATUS_OK;
        }
        else if (result == EINTR)
        {
            return MXL_ERR_INTERRUPTED;
        }
        return MXL_ERR_UNKNOWN;
    }

    return MXL_ERR_INVALID_ARG;
}

