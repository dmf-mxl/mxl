// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Region.hpp"
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <sys/uio.h>
#include <mxl-internal/DiscreteFlowData.hpp>
#include <mxl-internal/Flow.hpp>
#include "mxl-internal/ContinuousFlowData.hpp"
#include "mxl-internal/Instance.hpp"
#include "mxl/dataformat.h"
#include "mxl/fabrics.h"
#include "mxl/flow.h"
#include "mxl/mxl.h"
#include "Exception.hpp"
#include "VariantUtils.hpp"

namespace mxl::lib::fabrics::ofi
{
    Region::Location Region::Location::host() noexcept
    {
        return {Region::Location::Host{}};
    }

    Region::Location Region::Location::cuda(int deviceId) noexcept
    {
        return {Region::Location::Cuda{deviceId}};
    }

    std::uint64_t Region::Location::id() const noexcept
    {
        return std::visit(
            overloaded{
                [](std::monostate) -> std::uint64_t { throw Exception::invalidState("Region type is not set"); },
                [](Host const&) -> std::uint64_t { return 0; }, // Host region has no device ID
                [](Cuda const& cuda) -> std::uint64_t
                {
                    return static_cast<std::uint64_t>(cuda._deviceId);
                } // Cuda region returns its device ID
            },
            _inner);
    }

    ::fi_hmem_iface Region::Location::iface() const noexcept
    {
        return std::visit(
            overloaded{
                [](std::monostate) -> ::fi_hmem_iface { throw Exception::invalidState("Region type is not set"); },
                [](Host const&) -> ::fi_hmem_iface { return FI_HMEM_SYSTEM; },
                [](Cuda const&) -> ::fi_hmem_iface
                {
                    return FI_HMEM_CUDA;
                } // Cuda region returns its device ID
            },
            _inner);
    }

    bool Region::Location::isHost() const noexcept
    {
        return std::holds_alternative<Host>(_inner);
    }

    std::string Region::Location::toString() const noexcept
    {
        return std::visit(
            overloaded{
                [](std::monostate) -> std::string { throw Exception::invalidState("Region type is not set"); },
                [](Location::Host const&) -> std::string { return "host"; },
                [&](Location::Cuda const&) -> std::string { return fmt::format("cuda, id={}", id()); },
            },
            _inner);
    }

    ::iovec const* Region::asIovec() const noexcept
    {
        return &_iovec;
    }

    ::iovec Region::toIovec() const noexcept
    {
        return _iovec;
    }

    // Region implementations
    ::iovec Region::iovecFromRegion(std::uintptr_t base, std::size_t size) noexcept
    {
        return ::iovec{.iov_base = reinterpret_cast<void*>(base), .iov_len = size};
    }

    ::iovec const* RegionGroup::asIovec() const noexcept
    {
        return _iovecs.data();
    }

    std::vector<::iovec> RegionGroup::iovecsFromGroup(std::vector<Region> const& group) noexcept
    {
        std::vector<::iovec> iovecs;
        std::ranges::transform(group, std::back_inserter(iovecs), [](Region const& reg) { return reg.toIovec(); });
        return iovecs;
    }

    MxlRegions MxlRegions::forReader(::mxlFlowReader reader)
    {
        return mxlFabricsRegionsFromFlow(mxl::lib::to_FlowReader(reader)->getFlowData());
    }

    MxlRegions MxlRegions::forWriter(::mxlFlowWriter writer)
    {
        return mxlFabricsRegionsFromMutableFlow(mxl::lib::to_FlowWriter(writer)->getFlowData());
    }

    std::vector<Region> const& MxlRegions::regions() const noexcept
    {
        return _regions;
    }

    DataLayout const& MxlRegions::dataLayout() const noexcept
    {
        return _layout;
    }

    std::uint32_t MxlRegions::maxSyncBatchSize() const noexcept
    {
        return _maxSyncBatchSize;
    }

    std::size_t MxlRegions::regionsPerGrain() const noexcept
    {
        return _regionsPerGrain;
    }

    MxlRegions mxlFabricsRegionsFromMutableFlow(FlowData& flow)
    {
        auto mxlRegions = mxlFabricsRegionsFromFlow(flow);

        if (mxlIsDiscreteDataFormat(static_cast<int>(flow.flowInfo()->config.common.format)))
        {
            auto& discreteFlow = static_cast<DiscreteFlowData&>(flow);
            auto const stride = mxlRegions.regionsPerGrain();

            if ((stride == 0) || ((mxlRegions._regions.size() % stride) != 0) ||
                ((mxlRegions._regions.size() / stride) != discreteFlow.grainCount()))
            {
                throw Exception::invalidState("Unexpected number of regions in discrete flow (regions={}, grains={}, stride={})",
                    mxlRegions._regions.size(),
                    discreteFlow.grainCount(),
                    stride);
            }

            for (std::size_t i = 0; i < discreteFlow.grainCount(); ++i)
            {
                auto& headerRegion = mxlRegions._regions[i * stride];
                headerRegion.grainIndexPtr = &discreteFlow.grainAt(i)->header.info.index;
                headerRegion.validSlicesPtr = &discreteFlow.grainAt(i)->header.info.validSlices;
            }
        }

        return mxlRegions;
    }

    MxlRegions mxlFabricsRegionsFromFlow(FlowData const& flow)
    {
        static_assert(sizeof(GrainHeader) == 8192,
            "GrainHeader type size changed! The Fabrics API makes assumptions on the memory layout of a flow, please review the code below if the "
            "change is intended!");

        if (mxlIsDiscreteDataFormat(static_cast<int>(flow.flowInfo()->config.common.format)))
        {
            auto const& discreteFlow = static_cast<DiscreteFlowData const&>(flow);
            auto const payloadLocation = static_cast<mxlPayloadLocation>(flow.flowInfo()->config.common.payloadLocation);
            auto regions = std::vector<Region>{};
            auto regionsPerGrain = std::size_t{1};

            if (payloadLocation == MXL_PAYLOAD_LOCATION_HOST_MEMORY)
            {
                regions.reserve(discreteFlow.grainCount());
                for (auto i = std::size_t{0}; i < discreteFlow.grainCount(); ++i)
                {
                    auto const* grain = discreteFlow.grainAt(i);
                    auto const grainInfoBaseAddr = reinterpret_cast<std::uintptr_t>(grain);
                    auto const grainInfoSize = sizeof(GrainHeader);
                    auto const grainPayloadSize = grain->header.info.grainSize;

                    // Host-mapped payloads remain embedded after the grain header (single contiguous MR).
                    regions.emplace_back(grainInfoBaseAddr, grainInfoSize + grainPayloadSize, nullptr, nullptr, Region::Location::host());
                }
            }
            else if (payloadLocation == MXL_PAYLOAD_LOCATION_DEVICE_MEMORY)
            {
                regionsPerGrain = 2;
                regions.reserve(discreteFlow.grainCount() * regionsPerGrain);

                for (auto i = std::size_t{0}; i < discreteFlow.grainCount(); ++i)
                {
                    auto const* grain = discreteFlow.grainAt(i);
                    auto const headerBase = reinterpret_cast<std::uintptr_t>(grain);

                    mxlPayloadView payloadView{};
                    if (auto const status = discreteFlow.payloadViewAt(i, &payloadView); status != MXL_STATUS_OK)
                    {
                        throw Exception::make(status,
                            "Failed to resolve device grain payload for fabrics registration at slot {} (status {})",
                            i,
                            static_cast<int>(status));
                    }
                    if (payloadView.kind != MXL_PAYLOAD_KIND_DEVICE_PTR)
                    {
                        throw Exception::make(MXL_ERR_UNSUPPORTED_OPERATION,
                            "Fabrics discrete device flows currently require DEVICE_PTR payloads (got kind {})",
                            payloadView.kind);
                    }
                    if (payloadView.u.devicePtr == 0)
                    {
                        throw Exception::invalidState("Device payload pointer is null at grain slot {}", i);
                    }

                    regions.emplace_back(headerBase, sizeof(GrainHeader), nullptr, nullptr, Region::Location::host());
                    regions.emplace_back(static_cast<std::uintptr_t>(payloadView.u.devicePtr),
                        payloadView.grainSize,
                        nullptr,
                        nullptr,
                        Region::Location::cuda(payloadView.deviceIndex));
                }
            }
            else
            {
                throw Exception::make(MXL_ERR_UNSUPPORTED_OPERATION, "Unsupported payload location {}", static_cast<int>(payloadLocation));
            }

            return {std::move(regions),
                DataLayout::fromDiscrete(std::to_array(discreteFlow.flowInfo()->config.discrete.sliceSizes)),
                discreteFlow.flowInfo()->config.common.maxSyncBatchSizeHint,
                regionsPerGrain};
        }
        else if (mxlIsContinuousDataFormat(static_cast<int>(flow.flowInfo()->config.common.format)))
        {
            if (flow.flowInfo()->config.common.payloadLocation != MXL_PAYLOAD_LOCATION_HOST_MEMORY)
            {
                throw Exception::make(MXL_ERR_UNSUPPORTED_OPERATION, "Continuous flows with non-host payloads are not supported by fabrics");
            }

            auto const& continuousFlow = static_cast<ContinuousFlowData const&>(flow);
            auto regions = std::vector<Region>{};

            // For the continuous flow, the data layout is a single contiguous buffer
            regions.emplace_back(reinterpret_cast<std::uintptr_t>(continuousFlow.channelData()),
                continuousFlow.channelDataSize(),
                nullptr,
                nullptr,
                Region::Location::host());

            return {std::move(regions),
                DataLayout::fromContinuous(continuousFlow.sampleWordSize(), continuousFlow.channelCount(), continuousFlow.channelBufferLength()),
                continuousFlow.flowInfo()->config.common.maxSyncBatchSizeHint,
                1};
        }
        else
        {
            throw Exception::make(MXL_ERR_UNKNOWN, "Unsupported flow fromat {}", flow.flowInfo()->config.common.format);
        }
    }

    namespace
    {
        std::size_t headerRegionIndex(std::uint16_t slot, std::size_t regionsPerGrain, std::size_t regionCount)
        {
            if ((regionsPerGrain == 0) || ((regionCount % regionsPerGrain) != 0))
            {
                throw Exception::invalidState("Invalid regionsPerGrain {} for region count {}", regionsPerGrain, regionCount);
            }

            auto const grainCount = regionCount / regionsPerGrain;
            if (slot >= grainCount)
            {
                throw Exception::invalidArgument("Invalid ring buffer slot number: {}, grain count: {}", slot, grainCount);
            }

            return static_cast<std::size_t>(slot) * regionsPerGrain;
        }
    }

    std::uint64_t getGrainIndexInRingSlot(std::vector<Region> const& regions, std::uint16_t slot, std::size_t regionsPerGrain)
    {
        auto const index = headerRegionIndex(slot, regionsPerGrain, regions.size());
        if (regions[index].grainIndexPtr == nullptr)
        {
            throw Exception::invalidState("Grain index pointer is not set for ring slot {}", slot);
        }
        return *regions[index].grainIndexPtr;
    }

    void setValidSlicesForGrain(std::vector<Region> const& regions, std::uint16_t slot, std::uint16_t validSlices, std::size_t regionsPerGrain)
    {
        auto const index = headerRegionIndex(slot, regionsPerGrain, regions.size());
        if (regions[index].validSlicesPtr == nullptr)
        {
            throw Exception::invalidState("Valid slices pointer is not set for ring slot {}", slot);
        }
        *regions[index].validSlicesPtr = validSlices;
    }

    bool regionsNeedHmem(std::vector<Region> const& regions) noexcept
    {
        return std::ranges::any_of(regions, [](Region const& region) { return !region.loc.isHost(); });
    }
}
