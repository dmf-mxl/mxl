// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "mxl/fabrics.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mxl-internal/FlowReader.hpp>
#include <mxl-internal/Instance.hpp>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include <mxl/mxl.h>
#include "internal/Exception.hpp"
#include "internal/FabricInstance.hpp"
#include "internal/ImmData.hpp"
#include "internal/Initiator.hpp"
#include "internal/Provider.hpp"
#include "internal/Region.hpp"
#include "internal/Target.hpp"
#include "internal/TargetInfo.hpp"
#include "mxl/flow.h"
#include "mxl/platform.h"

namespace ofi = mxl::lib::fabrics::ofi;

template<typename F>
mxlStatus try_run(F func, std::string_view errMsg)
{
    try
    {
        return func();
    }
    catch (ofi::Exception& e)
    {
        if (e.status() == MXL_ERR_UNKNOWN)
        {
            MXL_ERROR("{}: {}", errMsg, e.what());
        }

        return e.status();
    }
    catch (std::exception& e)
    {
        MXL_ERROR("{}: {}", errMsg, e.what());

        return MXL_ERR_UNKNOWN;
    }
    catch (...)
    {
        MXL_ERROR("{}", errMsg);

        return MXL_ERR_UNKNOWN;
    }
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsForFlowReader(mxlFlowReader in_reader, mxlFabricsRegions* out_regions)
{
    if (in_reader == nullptr || out_regions == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto reader = ::mxl::lib::to_FlowReader(in_reader);

            // We are leaking the ownership, the user is responsible for calling mxlFabricsRegionsFree to free the memory.
            auto regionPtr = std::make_unique<ofi::MxlRegions>(ofi::mxlFabricsRegionsFromFlow(reader->getFlowData())).release();

            *out_regions = regionPtr->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to create regions object");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsForFlowWriter(mxlFlowWriter in_writer, mxlFabricsRegions* out_regions)
{
    if (in_writer == nullptr || out_regions == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto writer = ::mxl::lib::to_FlowWriter(in_writer);

            // We are leaking the ownership, the user is responsible for calling mxlFabricsRegionsFree to free the memory.
            auto regionPtr = std::make_unique<ofi::MxlRegions>(ofi::mxlFabricsRegionsFromFlow(writer->getFlowData())).release();

            *out_regions = regionPtr->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to create regions object");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsRegionsFree(mxlFabricsRegions in_regions)
{
    if (in_regions == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto regions = ofi::MxlRegions::fromAPI(in_regions);
            delete regions;

            return MXL_STATUS_OK;
        },
        "Failed to free regions object");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateInstance(mxlInstance in_instance, mxlFabricsInstance* out_fabricsInstance)
{
    if (in_instance == nullptr || out_fabricsInstance == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            *out_fabricsInstance = reinterpret_cast<mxlFabricsInstance>(
                new ofi::FabricsInstance(reinterpret_cast<::mxl::lib::Instance*>(in_instance)));

            return MXL_STATUS_OK;
        },
        "Failed to create fabrics instance");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsDestroyInstance(mxlFabricsInstance in_instance)
{
    if (in_instance == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            delete ofi::FabricsInstance::fromAPI(in_instance);

            return MXL_STATUS_OK;
        },
        "Failed to destroy fabrics instance");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateTarget(mxlFabricsInstance in_fabricsInstance, mxlFabricsTarget* out_target)
{
    if (in_fabricsInstance == nullptr || out_target == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto instance = ofi::FabricsInstance::fromAPI(in_fabricsInstance);
            *out_target = instance->createTarget()->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to create target");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsDestroyTarget(mxlFabricsInstance in_fabricsInstance, mxlFabricsTarget in_target)
{
    if (in_fabricsInstance == nullptr || in_target == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto instance = ofi::FabricsInstance::fromAPI(in_fabricsInstance);
            auto target = ofi::TargetWrapper::fromAPI(in_target);

            instance->destroyTarget(target);

            return MXL_STATUS_OK;
        },
        "Failed to destroy target");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetSetup(mxlFabricsTarget in_target, mxlFabricsTargetConfig* in_config, mxlFabricsTargetInfo* out_info)
{
    if (in_target == nullptr || in_config == nullptr || out_info == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            // Set up the target, release the returned unique_ptr, convert to external API type, assign the the pointer location
            // passed by the user.
            *out_info = ofi::TargetWrapper::fromAPI(in_target)->setup(*in_config).release()->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to set up target");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetReadNonBlocking(mxlFabricsTarget in_target, uint16_t* out_entryIndex, uint16_t* out_sliceIndex)
{
    if (in_target == nullptr || out_entryIndex == nullptr || out_sliceIndex == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto target = ofi::TargetWrapper::fromAPI(in_target);
            if (auto res = target->read(); res.immData)
            {
                auto [entryIndex, sliceIndex] = ofi::ImmDataGrain{*res.immData}.unpack();
                *out_entryIndex = entryIndex;
                *out_sliceIndex = sliceIndex;

                return MXL_STATUS_OK;
            }

            return MXL_ERR_NOT_READY;
        },
        "Failed to try for new grain");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetRead(mxlFabricsTarget in_target, uint16_t* out_entryIndex, uint16_t* out_sliceIndex, uint16_t in_timeoutMs)
{
    if (in_target == nullptr || out_entryIndex == nullptr || out_sliceIndex == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto target = ofi::TargetWrapper::fromAPI(in_target);
            if (auto res = target->readBlocking(std::chrono::milliseconds(in_timeoutMs)); res.immData)
            {
                auto [entryIndex, sliceIndex] = ofi::ImmDataGrain{*res.immData}.unpack();
                *out_entryIndex = entryIndex;
                *out_sliceIndex = sliceIndex;

                return MXL_STATUS_OK;
            }

            return MXL_ERR_NOT_READY;
        },
        "Failed to wait for new grain");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateInitiator(mxlFabricsInstance in_fabricsInstance, mxlFabricsInitiator* out_initiator)
{
    if (in_fabricsInstance == nullptr || out_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto instance = ofi::FabricsInstance::fromAPI(in_fabricsInstance);
            *out_initiator = instance->createInitiator()->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to create initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsDestroyInitiator(mxlFabricsInstance in_fabricsInstance, mxlFabricsInitiator in_initiator)
{
    if (in_fabricsInstance == nullptr || in_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            ofi::FabricsInstance::fromAPI(in_fabricsInstance)->destroyInitiator(ofi::InitiatorWrapper::fromAPI(in_initiator));

            return MXL_STATUS_OK;
        },
        "Failed to destroy initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorSetup(mxlFabricsInitiator in_initiator, mxlFabricsInitiatorConfig const* in_config)
{
    if (in_initiator == nullptr || in_config == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            ofi::InitiatorWrapper::fromAPI(in_initiator)->setup(*in_config);

            return MXL_STATUS_OK;
        },
        "Failed to set up initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorAddTarget(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo const in_targetInfo)
{
    if (in_initiator == nullptr || in_targetInfo == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto targetInfo = ofi::TargetInfo::fromAPI(in_targetInfo);
            ofi::InitiatorWrapper::fromAPI(in_initiator)->addTarget(*targetInfo);

            return MXL_STATUS_OK;
        },
        "Failed to add target to initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorRemoveTarget(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo const in_targetInfo)
{
    if (in_initiator == nullptr || in_targetInfo == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto targetInfo = ofi::TargetInfo::fromAPI(in_targetInfo);
            ofi::InitiatorWrapper::fromAPI(in_initiator)->removeTarget(*targetInfo);

            return MXL_STATUS_OK;
        },
        "Failed to remove target from initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorTransferGrain(mxlFabricsInitiator in_initiator, uint64_t in_grainIndex, uint16_t in_startSlice, uint16_t in_endSlice)
{
    if (in_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            ofi::InitiatorWrapper::fromAPI(in_initiator)->transferGrain(in_grainIndex, in_startSlice, in_endSlice);

            return MXL_STATUS_OK;
        },
        "Failed to transfer grain");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorMakeProgressNonBlocking(mxlFabricsInitiator in_initiator)
{
    if (in_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            if (ofi::InitiatorWrapper::fromAPI(in_initiator)->makeProgress())
            {
                return MXL_ERR_NOT_READY;
            }

            return MXL_STATUS_OK;
        },
        "Failed to make progress in the initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorMakeProgressBlocking(mxlFabricsInitiator in_initiator, uint16_t in_timeoutMs)
{
    if (in_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            if (ofi::InitiatorWrapper::fromAPI(in_initiator)->makeProgressBlocking(std::chrono::milliseconds(in_timeoutMs)))
            {
                return MXL_ERR_NOT_READY;
            }

            return MXL_STATUS_OK;
        },
        "Failed to make progress in the initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsProviderFromString(char const* in_string, mxlFabricsProvider* out_provider)
{
    if (in_string == nullptr || out_provider == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            if (auto provider = ofi::providerFromString(in_string); provider)
            {
                *out_provider = ofi::providerToAPI(*provider);
                return MXL_STATUS_OK;
            }

            return MXL_ERR_INVALID_ARG;
        },
        "Failed to convert string to provider");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsProviderToString(mxlFabricsProvider in_provider, char* out_string, size_t* in_out_stringSize)
{
    if (in_out_stringSize == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto providerEnumValueToString = [](char* outString, size_t* inOutStringSize, char const* providerString)
            {
                if (outString == nullptr)
                {
                    *inOutStringSize = ::strlen(providerString) + 1; // Null terminated.
                }
                else
                {
                    if (*inOutStringSize <= ::strlen(providerString))
                    {
                        return MXL_ERR_STRLEN;
                    }

                    ::strncpy(outString, providerString, *inOutStringSize);
                }

                return MXL_STATUS_OK;
            };

            switch (in_provider)
            {
                case MXL_FABRICS_PROVIDER_AUTO:  return providerEnumValueToString(out_string, in_out_stringSize, "auto");
                case MXL_FABRICS_PROVIDER_TCP:   return providerEnumValueToString(out_string, in_out_stringSize, "tcp");
                case MXL_FABRICS_PROVIDER_EFA:   return providerEnumValueToString(out_string, in_out_stringSize, "efa");
                case MXL_FABRICS_PROVIDER_VERBS: return providerEnumValueToString(out_string, in_out_stringSize, "verbs");
                case MXL_FABRICS_PROVIDER_SHM:   return providerEnumValueToString(out_string, in_out_stringSize, "shm");
                default:                         return MXL_ERR_INVALID_ARG;
            }
        },
        "Failed to convert provider to string");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetInfoFromString(char const* in_string, mxlFabricsTargetInfo* out_targetInfo)
{
    if (in_string == nullptr || out_targetInfo == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            *out_targetInfo = std::make_unique<ofi::TargetInfo>(ofi::TargetInfo::fromJSON(in_string)).release()->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to read target info from string");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetInfoToString(mxlFabricsTargetInfo const in_targetInfo, char* out_string, size_t* in_stringSize)
{
    if (in_targetInfo == nullptr || in_stringSize == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            std::stringstream ss;
            auto targetInfoString = ofi::TargetInfo::fromAPI(in_targetInfo)->toJSON();

            if (out_string == nullptr)
            {
                *in_stringSize = targetInfoString.length() + 1;
            }
            else
            {
                if (*in_stringSize <= targetInfoString.length())
                {
                    return MXL_ERR_STRLEN;
                }
                else
                {
                    ::strncpy(out_string, targetInfoString.c_str(), *in_stringSize);
                }
            }

            return MXL_STATUS_OK;
        },
        "Failed to serialize target info");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsFreeTargetInfo(mxlFabricsTargetInfo in_info)
{
    if (in_info == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            delete ofi::TargetInfo::fromAPI(in_info);

            return MXL_STATUS_OK;
        },
        "Failed to free target info object");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsExtGetRegions(mxlFabricsExtRegionsConfig const* in_config, mxlFabricsRegions* out_regions)
{
    if (in_config == nullptr || out_regions == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto regions = ofi::mxlFabricsRegionsFromUser(*in_config);

            // We are leaking the ownership, the user is responsible for calling mxlFabricsRegionsFree to free the memory.
            auto regionPtr = std::make_unique<ofi::MxlRegions>(regions).release();

            *out_regions = regionPtr->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to create regions object");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsExtInitiatorTransferGrain(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo const in_targetInfo, uint64_t in_localIndex,
    uint64_t in_remoteIndex, uint64_t in_payloadOffset, uint16_t in_startSlice, uint16_t in_endSlice)
{
    if (in_initiator == nullptr || in_targetInfo == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return try_run(
        [&]()
        {
            auto targetInfo = ofi::TargetInfo::fromAPI(in_targetInfo);
            ofi::InitiatorWrapper::fromAPI(in_initiator)
                ->transferGrainToTarget(targetInfo->id, in_localIndex, in_remoteIndex, in_payloadOffset, in_startSlice, in_endSlice);

            return MXL_STATUS_OK;
        },
        "Failed to transfer grain to target");
}
