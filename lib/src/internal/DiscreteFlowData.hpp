// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowData.hpp"

namespace mxl::lib
{
    ///
    /// Simple structure holding the shared memory resources of discrete flows.
    ///
    class DiscreteFlowData : public FlowData
    {
    public:
        explicit DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;
        DiscreteFlowData(char const* flowFilePath, AccessMode mode);

        std::size_t grainCount() const noexcept;

        Grain* emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize);

        Grain* grainAt(std::size_t i) noexcept;
        Grain const* grainAt(std::size_t i) const noexcept;

        mxlGrainInfo* grainInfoAt(std::size_t i) noexcept;
        mxlGrainInfo const* grainInfoAt(std::size_t i) const noexcept;

    private:
        std::vector<SharedMemoryInstance<Grain>> _grains;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline DiscreteFlowData::DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : FlowData{std::move(flowSegement)}
        , _grains{}
    {
        _grains.reserve(flowInfo()->discrete.grainCount);
    }

    inline DiscreteFlowData::DiscreteFlowData(char const* flowFilePath, AccessMode mode)
        : FlowData{flowFilePath, mode}
        , _grains{}
    {
        _grains.reserve(flowInfo()->discrete.grainCount);
    }

    inline std::size_t DiscreteFlowData::grainCount() const noexcept
    {
        return _grains.size();
    }

    inline Grain* DiscreteFlowData::emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize)
    {
        auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();
        return _grains.emplace_back(grainFilePath, mode, grainPayloadSize).get();
    }

    inline Grain* DiscreteFlowData::grainAt(std::size_t i) noexcept
    {
        return (i < _grains.size()) ? _grains[i].get() : nullptr;
    }

    inline Grain const* DiscreteFlowData::grainAt(std::size_t i) const noexcept
    {
        return (i < _grains.size()) ? _grains[i].get() : nullptr;
    }

    inline mxlGrainInfo* DiscreteFlowData::grainInfoAt(std::size_t i) noexcept
    {
        if (auto const grain = grainAt(i); grain != nullptr)
        {
            return &grain->header.info;
        }
        return nullptr;
    }

    inline mxlGrainInfo const* DiscreteFlowData::grainInfoAt(std::size_t i) const noexcept
    {
        if (auto const grain = grainAt(i); grain != nullptr)
        {
            return &grain->header.info;
        }
        return nullptr;
    }
}
