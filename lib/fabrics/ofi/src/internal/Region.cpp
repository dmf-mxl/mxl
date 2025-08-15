#include "Region.hpp"
#include <cstdint>
#include <algorithm>
#include <bits/types/struct_iovec.h>
#include "internal/DiscreteFlowData.hpp"
#include "internal/Flow.hpp"
#include "mxl/mxl.h"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    ::iovec const* Region::as_iovec() const noexcept
    {
        return &_iovec;
    }

    ::iovec Region::to_iovec() const noexcept
    {
        return _iovec;
    }

    // Region implementations
    ::iovec Region::iovecFromRegion(std::uintptr_t base, size_t size) noexcept
    {
        return ::iovec{.iov_base = reinterpret_cast<void*>(base), .iov_len = size};
    }

    // Region implementations

    std::vector<Region> const& RegionGroup::view() const noexcept
    {
        return _inner;
    }

    ::iovec const* RegionGroup::as_iovec() const noexcept
    {
        return _iovecs.data();
    }

    std::vector<::iovec> RegionGroup::iovecsFromGroup(std::vector<Region> const& group) noexcept
    {
        std::vector<::iovec> iovecs;
        std::ranges::transform(group, std::back_inserter(iovecs), [](Region const& reg) { return reg.to_iovec(); });
        return iovecs;
    }

    RegionGroups RegionGroups::fromFlow(FlowData& flow)
    {
        static_assert(sizeof(GrainHeader) == 8192,
            "GrainHeader type size changed! The Fabrics API makes assumptions on the memory layout of a flow, please review the code below if the "
            "change is intended!");

        if (!mxlIsDiscreteDataFormat(flow.flowInfo()->common.format))
        {
            throw Exception::make(MXL_ERR_UNKNOWN, "Non-discrete flows not supported for now");
        }

        auto& discreteFlow = static_cast<DiscreteFlowData&>(flow);

        std::vector<RegionGroup> regionGroups;

        for (std::size_t i = 0; i < discreteFlow.grainCount(); ++i)
        {
            auto grain = discreteFlow.grainAt(i);

            auto grainInfoBaseAddr = reinterpret_cast<std::uintptr_t>(discreteFlow.grainAt(i));
            auto grainInfoSize = sizeof(GrainHeader);
            auto grainPayloadSize = grain->header.info.grainSize;

            auto regionGroup = RegionGroup({
                Region{grainInfoBaseAddr, grainInfoSize + grainPayloadSize},
            });

            regionGroups.emplace_back(std::move(regionGroup));
        }

        return RegionGroups{std::move(regionGroups)};
    }

    RegionGroups* RegionGroups::fromAPI(mxlRegions regions) noexcept
    {
        return reinterpret_cast<RegionGroups*>(regions);
    }

    mxlRegions RegionGroups::toAPI() noexcept
    {
        return reinterpret_cast<mxlRegions>(this);
    }

    std::vector<RegionGroup> const& RegionGroups::view() const noexcept
    {
        return _inner;
    }

}
