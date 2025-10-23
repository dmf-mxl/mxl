// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <mxl/fabrics.h>
#include <mxl/mxl.h>

#define MXL_FABRICS_UNUSED(x) (void)x

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsForFlowReader(mxlFlowReader, mxlRegions*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsForFlowWriter(mxlFlowWriter, mxlRegions*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsFromUserBuffers(mxlFabricsMemoryRegion const*, size_t, mxlRegions*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsFree(mxlRegions)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateInstance(mxlInstance, mxlFabricsInstance*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsDestroyInstance(mxlFabricsInstance)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateTarget(mxlFabricsInstance, mxlFabricsTarget*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsDestroyTarget(mxlFabricsInstance, mxlFabricsTarget)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetSetup(mxlFabricsTarget, mxlTargetConfig*, mxlTargetInfo*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetTryNewGrain(mxlFabricsTarget, uint64_t*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetWaitForNewGrain(mxlFabricsTarget, uint64_t*, uint16_t)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateInitiator(mxlFabricsInstance, mxlFabricsInitiator*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsDestroyInitiator(mxlFabricsInstance, mxlFabricsInitiator)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorSetup(mxlFabricsInitiator, mxlInitiatorConfig const*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorAddTarget(mxlFabricsInitiator, mxlTargetInfo const)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorRemoveTarget(mxlFabricsInitiator, mxlTargetInfo const)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorTransferGrain(mxlFabricsInitiator, uint64_t)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorMakeProgressNonBlocking(mxlFabricsInitiator)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorMakeProgressBlocking(mxlFabricsInitiator, uint16_t)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsProviderFromString(char const*, mxlFabricsProvider*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsProviderToString(mxlFabricsProvider, char*, size_t*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetInfoFromString(char const*, mxlTargetInfo*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetInfoToString(mxlTargetInfo const, char*, size_t*)
{
    return MXL_ERR_INTERNAL;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsFreeTargetInfo(mxlTargetInfo)
{
    return MXL_ERR_INTERNAL;
}
