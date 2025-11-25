// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Initiator.hpp"
#include "mxl/fabrics.h"
#include "Exception.hpp"
#include "RCInitiator.hpp"
#include "RDMInitiator.hpp"

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

    void InitiatorWrapper::setup(mxlFabricsInitiatorConfig const& config)
    {
        if (_inner)
        {
            _inner.release();
        }

        switch (config.provider)
        {
            case MXL_FABRICS_PROVIDER_AUTO:
            case MXL_FABRICS_PROVIDER_TCP:
            case MXL_FABRICS_PROVIDER_VERBS:
            {
                _inner = RCInitiator::setup(config);
                return;
            }

            case MXL_FABRICS_PROVIDER_SHM:
            case MXL_FABRICS_PROVIDER_EFA:
            {
                _inner = RDMInitiator::setup(config);
                return;
            }
        }

        throw Exception::invalidArgument("Invalid provider value");
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

    bool InitiatorWrapper::makeProgress()
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        return _inner->makeProgress();
    }

    bool InitiatorWrapper::makeProgressBlocking(std::chrono::steady_clock::duration timeout)
    {
        if (!_inner)
        {
            throw Exception::invalidState("Initiator is not set up");
        }

        return _inner->makeProgressBlocking(timeout);
    }

}
