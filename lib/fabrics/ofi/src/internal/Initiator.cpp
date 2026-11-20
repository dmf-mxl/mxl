// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Initiator.hpp"
#include <mxl-internal/Logging.hpp>
#include "mxl/fabrics.h"
#include "Exception.hpp"
#include "FabricInfoHelpers.hpp"
#include "FabricInterfaceProbe.hpp"
#include "RCInitiator.hpp"
#include "RDMInitiator.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{

    InitiatorWrapper* InitiatorWrapper::fromAPI(mxlFabricsInitiator api) noexcept
    {
        return reinterpret_cast<InitiatorWrapper*>(api);
    }

    mxlFabricsInitiator InitiatorWrapper::toAPI() noexcept
    {
        return reinterpret_cast<mxlFabricsInitiator>(this);
    }

    void InitiatorWrapper::setup(mxlFabricsInitiatorConfig const& config, InitiatorSetupOptions const& options)
    {
        if (_inner)
        {
            _inner.reset();
        }

        // Device payloads are registered for reading with FI_HMEM, so the interface must support it.
        auto const needsHmem = regionsNeedHmem(MxlRegions::forReader(config.reader).regions());
        auto const interfaceConfig = interfaceConfigWithHmemIfNeeded(config.interface, needsHmem);
        auto const [info, provierConfig] = selectSourceInterface(interfaceConfig, /* target */ false);
        (void)provierConfig;

        if (needsHmem)
        {
            requireCapability(info.view(), FI_HMEM, "Selected fabric interface does not support FI_HMEM required by device grain payloads");
        }

        switch (info.view().endpointType())
        {
            case FI_EP_MSG: _inner = RCInitiator::setup(config, info.view(), options); break;
            case FI_EP_RDM: _inner = RDMInitiator::setup(config, info.view(), options); break;
            default:        throw Exception::invalidState("unsupported endpoint type");
        }
    }

    void InitiatorWrapper::addTarget(TargetInfo const& targetInfo)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        _inner->addTarget(targetInfo);
    }

    void InitiatorWrapper::removeTarget(TargetInfo const& targetInfo)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        _inner->removeTarget(targetInfo);
    }

    void InitiatorWrapper::transferGrain(std::uint64_t grainIndex, std::uint16_t startSlice, std::uint16_t endSlice)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        _inner->transferGrain(grainIndex, startSlice, endSlice);
    }

    void InitiatorWrapper::transferGrainToTarget(Endpoint::Id targetId, std::uint64_t localIndex, std::uint64_t remoteIndex,
        std::uint64_t payloadOffset, std::uint16_t startSlice, std::uint16_t endSlice)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        _inner->transferGrainToTarget(targetId, localIndex, remoteIndex, payloadOffset, startSlice, endSlice);
    }

    void InitiatorWrapper::transferSamples(std::uint64_t headIndex, std::size_t count)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        _inner->transferSamples(headIndex, count);
    }

    Initiator::MakeProgressResult InitiatorWrapper::makeProgress()
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        return _inner->makeProgress();
    }

    Initiator::MakeProgressResult InitiatorWrapper::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        return _inner->makeProgressBlocking(timeout);
    }

}
