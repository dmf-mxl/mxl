// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "mxl/fabrics.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <mxl-internal/FlowReader.hpp>
#include <mxl-internal/Instance.hpp>
#include <mxl-internal/Logging.hpp>
#include <picojson/picojson.h>
#include <rdma/fabric.h>
#include <mxl/mxl.h>
#include "internal/Exception.hpp"
#include "internal/FabricInstance.hpp"
#include "internal/FabricInterfaceProbe.hpp"
#include "internal/Initiator.hpp"
#include "internal/Provider.hpp"
#include "internal/Target.hpp"
#include "internal/TargetInfo.hpp"
#include "mxl/platform.h"
#include "VariantUtils.hpp"

namespace ofi = mxl::lib::fabrics::ofi;

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        /// Parse the optional target setup options JSON and return the requested completion
        /// queue depth, or std::nullopt (use the implementation default) when not specified.
        ///
        /// Recognized form: {"cqDepth": <positive integer>}
        std::optional<std::size_t> parseCqDepthOption(char const* options)
        {
            if ((options == nullptr) || (options[0] == '\0'))
            {
                return std::nullopt;
            }

            auto value = picojson::value{};
            auto const err = picojson::parse(value, options);
            if (!err.empty() || !value.is<picojson::object>())
            {
                throw Exception::invalidArgument("Invalid JSON target options: {}", err.empty() ? std::string{"expected an object"} : err);
            }

            auto const& root = value.get<picojson::object>();
            auto const it = root.find("cqDepth");
            if (it == root.end())
            {
                return std::nullopt;
            }
            if (!it->second.is<double>())
            {
                throw Exception::invalidArgument("cqDepth must be a number.");
            }
            auto const depth = it->second.get<double>();
            if (depth < 1.0)
            {
                throw Exception::invalidArgument("cqDepth must be greater or equal to 1.");
            }
            return static_cast<std::size_t>(depth);
        }

        template<typename F>
        mxlStatus try_run(F&& func, std::string_view errMsg)
        {
            try
            {
                return func();
            }
            catch (ofi::Exception& e)
            {
                MXL_ERROR("{}: {}", errMsg, e.what());

                return e.status();
            }
            catch (std::exception& e)
            {
                MXL_ERROR("{}: {}", errMsg, e.what());

                return MXL_ERR_UNKNOWN;
            }
            catch (...)
            {
                MXL_ERROR("{}: unknown exception occurred", errMsg);

                return MXL_ERR_UNKNOWN;
            }
        }
    }
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateInstance(mxlInstance in_instance, char const* options, mxlFabricsInstance* out_fabricsInstance)
{
    (void)options;
    if ((in_instance == nullptr) || (out_fabricsInstance == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
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

    return ofi::try_run(
        [&]()
        {
            delete ofi::FabricsInstance::fromAPI(in_instance);

            return MXL_STATUS_OK;
        },
        "Failed to destroy fabrics instance");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsGetInterfaces(mxlFabricsInstance in_instance, mxlFabricsInterfaceConfig const* query, mxlFabricsInterfaceList** list)
{
    if ((in_instance == nullptr) || (list == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto optQuery =
                (query == nullptr) ? std::nullopt : std::make_optional<std::reference_wrapper<::mxlFabricsInterfaceConfig const>>(std::cref(*query));
            *list = ofi::probeInterfaces(optQuery).toRawLinkedList();
            return MXL_STATUS_OK;
        },
        "Failed to get a list of interfaces");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsFreeInterfaceList(mxlFabricsInterfaceList* interfaceList)
{
    if (interfaceList == nullptr)
    {
        return MXL_STATUS_OK;
    }

    ofi::FabricInterfaceList::freeRawLinkedList(interfaceList);
    return MXL_STATUS_OK;
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateTarget(mxlFabricsInstance in_fabricsInstance, mxlFabricsTarget* out_target)
{
    if ((in_fabricsInstance == nullptr) || (out_target == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
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
    if ((in_fabricsInstance == nullptr) || (in_target == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
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
mxlStatus mxlFabricsTargetSetup(mxlFabricsTarget in_target, mxlFabricsTargetConfig const* in_config, char const* options,
    mxlFabricsTargetInfo* out_info)
{
    if ((in_target == nullptr) || (in_config == nullptr) || (out_info == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto const setupOptions = ofi::TargetSetupOptions{.cqDepth = ofi::parseCqDepthOption(options)};

            // Set up the target, release the returned unique_ptr, convert to external API type, assign the the pointer location
            // passed by the user.
            *out_info = ofi::TargetWrapper::fromAPI(in_target)->setup(*in_config, setupOptions).release()->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to set up target");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetReadGrainNonBlocking(mxlFabricsTarget in_target, uint64_t* out_grainIndex)
{
    if ((in_target == nullptr) || (out_grainIndex == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto const result = ofi::TargetWrapper::fromAPI(in_target)->read();
            if (!result)
            {
                return MXL_ERR_NOT_READY;
            }

            if (std::holds_alternative<ofi::Target::Interrupted>(*result))
            {
                return MXL_ERR_INTERRUPTED;
            }

            auto const grainResult = std::get_if<ofi::Target::GrainReadResult>(&*result);
            if (grainResult == nullptr)
            {
                return MXL_ERR_INVALID_STATE;
            }

            *out_grainIndex = grainResult->grainIndex;
            return MXL_STATUS_OK;
        },
        "Failed to try for new grain");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetReadGrain(mxlFabricsTarget in_target, uint16_t in_timeoutMs, uint64_t* out_grainIndex)
{
    if ((in_target == nullptr) || (out_grainIndex == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto const result = ofi::TargetWrapper::fromAPI(in_target)->readBlocking(std::chrono::milliseconds(in_timeoutMs));
            if (!result)
            {
                return MXL_ERR_NOT_READY;
            }

            if (std::holds_alternative<ofi::Target::Interrupted>(*result))
            {
                return MXL_ERR_INTERRUPTED;
            }

            auto const grainResult = std::get_if<ofi::Target::GrainReadResult>(&*result);
            if (grainResult == nullptr)
            {
                return MXL_ERR_INVALID_STATE;
            }

            *out_grainIndex = grainResult->grainIndex;
            return MXL_STATUS_OK;
        },
        "Failed to wait for new grain");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetReadSamplesNonBlocking(mxlFabricsTarget in_target, uint64_t* out_headIndex, size_t* out_count)
{
    if ((in_target == nullptr) || (out_headIndex == nullptr) || (out_count == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto const result = ofi::TargetWrapper::fromAPI(in_target)->read();
            if (!result)
            {
                return MXL_ERR_NOT_READY;
            }

            if (std::holds_alternative<ofi::Target::Interrupted>(*result))
            {
                return MXL_ERR_INTERRUPTED;
            }

            auto const sampleResult = std::get_if<ofi::Target::SampleReadResult>(&*result);
            if (sampleResult == nullptr)
            {
                return MXL_ERR_INVALID_STATE;
            }

            *out_headIndex = sampleResult->headIndex;
            *out_count = sampleResult->count;
            return MXL_STATUS_OK;
        },
        "Failed to try for new samples");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetReadSamples(mxlFabricsTarget in_target, uint16_t in_timeoutMs, uint64_t* out_headIndex, size_t* out_count)
{
    if ((in_target == nullptr) || (out_headIndex == nullptr) || (out_count == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto const result = ofi::TargetWrapper::fromAPI(in_target)->readBlocking(std::chrono::milliseconds(in_timeoutMs));
            if (!result)
            {
                return MXL_ERR_NOT_READY;
            }

            if (std::holds_alternative<ofi::Target::Interrupted>(*result))
            {
                return MXL_ERR_INTERRUPTED;
            }

            auto const sampleResult = std::get_if<ofi::Target::SampleReadResult>(&*result);
            if (sampleResult == nullptr)
            {
                return MXL_ERR_INVALID_STATE;
            }

            *out_headIndex = sampleResult->headIndex;
            *out_count = sampleResult->count;
            return MXL_STATUS_OK;
        },
        "Failed to wait for new samples");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsCreateInitiator(mxlFabricsInstance in_fabricsInstance, mxlFabricsInitiator* out_initiator)
{
    if ((in_fabricsInstance == nullptr) || (out_initiator == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
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
    if ((in_fabricsInstance == nullptr) || (in_initiator == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            ofi::FabricsInstance::fromAPI(in_fabricsInstance)->destroyInitiator(ofi::InitiatorWrapper::fromAPI(in_initiator));

            return MXL_STATUS_OK;
        },
        "Failed to destroy initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorSetup(mxlFabricsInitiator in_initiator, mxlFabricsInitiatorConfig const* in_config, char const* options)
{
    (void)options;
    if ((in_initiator == nullptr) || (in_config == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            ofi::InitiatorWrapper::fromAPI(in_initiator)->setup(*in_config);

            return MXL_STATUS_OK;
        },
        "Failed to set up initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorAddTarget(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo in_targetInfo)
{
    if ((in_initiator == nullptr) || (in_targetInfo == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto targetInfo = ofi::TargetInfo::fromAPI(in_targetInfo);
            ofi::InitiatorWrapper::fromAPI(in_initiator)->addTarget(*targetInfo);

            return MXL_STATUS_OK;
        },
        "Failed to add target to initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorRemoveTarget(mxlFabricsInitiator in_initiator, mxlFabricsTargetInfo in_targetInfo)
{
    if ((in_initiator == nullptr) || (in_targetInfo == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
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

    return ofi::try_run(
        [&]()
        {
            ofi::InitiatorWrapper::fromAPI(in_initiator)->transferGrain(in_grainIndex, in_startSlice, in_endSlice);

            return MXL_STATUS_OK;
        },
        "Failed to transfer grain");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorTransferSamples(mxlFabricsInitiator in_initiator, uint64_t in_headIndex, size_t in_count)
{
    if (in_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            ofi::InitiatorWrapper::fromAPI(in_initiator)->transferSamples(in_headIndex, in_count);

            return MXL_STATUS_OK;
        },
        "Failed to transfer samples");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsInitiatorMakeProgressNonBlocking(mxlFabricsInitiator in_initiator)
{
    if (in_initiator == nullptr)
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            auto const result = ofi::InitiatorWrapper::fromAPI(in_initiator)->makeProgress();
            return std::visit(
                ofi::overloaded{
                    [](ofi::Initiator::Ready) { return MXL_STATUS_OK; },
                    [](ofi::Initiator::NotReady) { return MXL_ERR_NOT_READY; },
                    [](ofi::Initiator::Interrupted) { return MXL_ERR_INTERRUPTED; },
                },
                result);
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

    return ofi::try_run(
        [&]()
        {
            auto const result = ofi::InitiatorWrapper::fromAPI(in_initiator)->makeProgressBlocking(std::chrono::milliseconds(in_timeoutMs));
            return std::visit(
                ofi::overloaded{
                    [](ofi::Initiator::Ready) { return MXL_STATUS_OK; },
                    [](ofi::Initiator::NotReady) { return MXL_ERR_NOT_READY; },
                    [](ofi::Initiator::Interrupted) { return MXL_ERR_INTERRUPTED; },
                },
                result);
        },
        "Failed to make progress in the initiator");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsProviderFromString(char const* in_string, mxlFabricsProvider* out_provider)
{
    if ((in_string == nullptr) || (out_provider == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
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

    return ofi::try_run(
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
                case MXL_FABRICS_PROVIDER_ANY:   return providerEnumValueToString(out_string, in_out_stringSize, "any");
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
    if ((in_string == nullptr) || (out_targetInfo == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
            *out_targetInfo = std::make_unique<ofi::TargetInfo>(ofi::TargetInfo::fromJSON(in_string)).release()->toAPI();

            return MXL_STATUS_OK;
        },
        "Failed to read target info from string");
}

extern "C" MXL_EXPORT
mxlStatus mxlFabricsTargetInfoToString(mxlFabricsTargetInfo in_targetInfo, char* out_string, size_t* in_stringSize)
{
    if ((in_targetInfo == nullptr) || (in_stringSize == nullptr))
    {
        return MXL_ERR_INVALID_ARG;
    }

    return ofi::try_run(
        [&]()
        {
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
                    std::strncpy(out_string, targetInfoString.c_str(), *in_stringSize);
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

    return ofi::try_run(
        [&]()
        {
            delete ofi::TargetInfo::fromAPI(in_info);

            return MXL_STATUS_OK;
        },
        "Failed to free target info object");
}
