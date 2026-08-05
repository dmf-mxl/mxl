// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <stdexcept>
#include <vector>
#include <fmt/format.h>
#include <mxl/mxl.h>
#include "Flow.hpp"
#include "FlowData.hpp"
#include "GrainPayloadAllocator.hpp"

namespace mxl::lib
{
    ///
    /// Shared memory resources of a discrete flow: grain headers (always host-mapped) plus a
    /// pluggable payload allocator that describes where grain payload bytes live.
    ///
    class DiscreteFlowData : public FlowData
    {
    public:
        explicit DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;
        DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

        std::size_t grainCount() const noexcept;

        Grain* emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize);

        Grain* grainAt(std::size_t i) noexcept;
        Grain const* grainAt(std::size_t i) const noexcept;

        mxlGrainInfo* grainInfoAt(std::size_t i) noexcept;
        mxlGrainInfo const* grainInfoAt(std::size_t i) const noexcept;

        void setPayloadAllocator(std::unique_ptr<GrainPayloadAllocator> allocator) noexcept;
        [[nodiscard]]
        GrainPayloadAllocator const& payloadAllocator() const;

        /**
         * Resolve the payload view for ring slot \p i via the attached payload allocator.
         */
        [[nodiscard]]
        mxlStatus payloadViewAt(std::size_t i, mxlPayloadView* outView) const noexcept;

    private:
        std::vector<SharedMemoryInstance<Grain>> _grains;
        std::unique_ptr<GrainPayloadAllocator> _payloadAllocator;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline DiscreteFlowData::DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : FlowData{std::move(flowSegement)}
        , _grains{}
        , _payloadAllocator{}
    {
        _grains.reserve(flowInfo()->config.discrete.grainCount);
    }

    inline DiscreteFlowData::DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
        : FlowData{flowFilePath, mode, lockMode}
        , _grains{}
        , _payloadAllocator{}
    {
        _grains.reserve(flowInfo()->config.discrete.grainCount);
    }

    inline std::size_t DiscreteFlowData::grainCount() const noexcept
    {
        return _grains.size();
    }

    inline Grain* DiscreteFlowData::emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize)
    {
        auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();
        auto grain = SharedMemoryInstance<Grain>{grainFilePath, mode, grainPayloadSize, LockMode::Shared};

        if (!this->created())
        {
            // Check for the version of the grain data structure in the memory that was just mapped.
            if (grain.get()->header.info.version != GRAIN_HEADER_VERSION)
            {
                throw std::invalid_argument{
                    fmt::format("Unsupported grain version: {}, supported version is: {}", grain.get()->header.info.version, GRAIN_HEADER_VERSION)};
            }
        }

        return _grains.emplace_back(std::move(grain)).get();
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

    inline void DiscreteFlowData::setPayloadAllocator(std::unique_ptr<GrainPayloadAllocator> allocator) noexcept
    {
        _payloadAllocator = std::move(allocator);
    }

    inline GrainPayloadAllocator const& DiscreteFlowData::payloadAllocator() const
    {
        if (!_payloadAllocator)
        {
            throw std::runtime_error{"Discrete flow has no payload allocator."};
        }
        return *_payloadAllocator;
    }

    inline mxlStatus DiscreteFlowData::payloadViewAt(std::size_t i, mxlPayloadView* outView) const noexcept
    {
        if ((outView == nullptr) || !_payloadAllocator)
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto const grain = grainAt(i);
        if (grain == nullptr)
        {
            return MXL_ERR_INVALID_ARG;
        }

        return _payloadAllocator->viewAt(i, grain, outView);
    }
}
