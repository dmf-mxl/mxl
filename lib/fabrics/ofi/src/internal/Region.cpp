// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Region.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string_view>
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

            if (mxlRegions._regions.size() != discreteFlow.grainCount() * mxlRegions._regionsPerGrain)
            {
                throw Exception::invalidState("Unexpected number of grains in discrete flow");
            }

            // The grain header is the first region of each grain.
            for (std::size_t i = 0; i < discreteFlow.grainCount(); ++i)
            {
                auto& headerRegion = mxlRegions._regions[i * mxlRegions._regionsPerGrain];
                headerRegion.grainIndexPtr = &discreteFlow.grainAt(i)->header.info.index;
                headerRegion.validSlicesPtr = &discreteFlow.grainAt(i)->header.info.validSlices;
            }
        }

        return mxlRegions;
    }

    namespace
    {
        /** Storage type of payload in CUDA device memory. Defined by the cuda-linear backend, see cuda_device_pointer.h. */
        constexpr auto cudaDevicePointerStorageType = "cuda-device-pointer";

        /** Byte offset of the device index in mxlGrainStorageLayout::reserved for cuda-device-pointer storage. The layout is
         * mxlCudaGrainStorageLayout: int32_t deviceIndex first. The fabrics library does not depend on the backend header.
         */
        int cudaDeviceIndexFromLayout(mxlGrainStorageLayout const& layout) noexcept
        {
            auto deviceIndex = std::int32_t{-1};
            std::memcpy(&deviceIndex, layout.reserved, sizeof(deviceIndex));
            return deviceIndex;
        }

        /** Pointer of a slot in process memory. Only the storage types fabrics knows are supported.
         * The cuda-device-pointer slot starts with the device pointer (mxlCudaGrainStorage::pointer).
         */
        std::uintptr_t slotPointer(std::string_view storageType, mxlGrainStorage const& slot)
        {
            if (storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER)
            {
                return reinterpret_cast<std::uintptr_t>(slot.host.pointer);
            }
            if (storageType == cudaDevicePointerStorageType)
            {
                void* pointer = nullptr;
                std::memcpy(&pointer, slot.reserved, sizeof(pointer));
                return reinterpret_cast<std::uintptr_t>(pointer);
            }
            throw Exception::make(MXL_ERR_UNSUPPORTED_OPERATION, "Fabrics does not support the payload storage type '{}'", storageType);
        }

        /** Memory location of the payloads of a storage type. */
        Region::Location slotLocation(mxlGrainStorageLayout const& layout)
        {
            auto const storageType = std::string_view{layout.storageType};
            if (storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER)
            {
                return Region::Location::host();
            }
            if (storageType == cudaDevicePointerStorageType)
            {
                auto const deviceIndex = cudaDeviceIndexFromLayout(layout);
                if (deviceIndex < 0)
                {
                    throw Exception::invalidState("The payload is in CUDA device memory, but this process has no device to access it");
                }
                return Region::Location::cuda(deviceIndex);
            }
            throw Exception::make(MXL_ERR_UNSUPPORTED_OPERATION, "Fabrics does not support the payload storage type '{}'", layout.storageType);
        }
    }

    MxlRegions mxlFabricsRegionsFromFlow(FlowData const& flow)
    {
        static_assert(sizeof(GrainHeader) == 8192,
            "GrainHeader type size changed! The Fabrics API makes assumptions on the memory layout of a flow, please review the code below if the "
            "change is intended!");

        if (mxlIsDiscreteDataFormat(static_cast<int>(flow.flowInfo()->config.common.format)))
        {
            auto const& discreteFlow = static_cast<DiscreteFlowData const&>(flow);
            auto regions = std::vector<Region>{};
            auto regionsPerGrain = std::size_t{1};

            if (flow.flowInfo()->version == FLOW_DATA_VERSION_PAYLOAD_BACKEND)
            {
                // The payload is held by a payload backend, so the grain file has only the header. Each grain registers two regions: the
                // header in host memory, then the payload at the address the storage gives for the slot.
                regionsPerGrain = 2;
                auto const grainCount = discreteFlow.grainCount();
                regions.reserve(grainCount * regionsPerGrain);

                auto slots = std::vector<mxlGrainStorage>(grainCount);
                if (auto const status = discreteFlow.mapSlots(static_cast<std::uint32_t>(grainCount), slots.data()); status != MXL_STATUS_OK)
                {
                    throw Exception::make(status, "Failed to map the payload storage of the flow for fabrics (status {})", static_cast<int>(status));
                }

                auto const storageLayout = discreteFlow.storageLayout();
                auto const location = slotLocation(storageLayout);
                auto const storageType = std::string_view{storageLayout.storageType};

                for (auto i = std::size_t{0}; i < grainCount; ++i)
                {
                    auto const* grain = discreteFlow.grainAt(i);
                    auto const pointer = slotPointer(storageType, slots[i]);
                    if (pointer == 0)
                    {
                        throw Exception::invalidState("Payload pointer is null at grain slot {}", i);
                    }

                    regions.emplace_back(reinterpret_cast<std::uintptr_t>(grain), sizeof(GrainHeader), nullptr, nullptr, Region::Location::host());
                    regions.emplace_back(pointer, grain->header.info.grainSize, nullptr, nullptr, location);
                }
            }
            else
            {
                regions.reserve(discreteFlow.grainCount());
                for (auto i = std::size_t{0}; i < discreteFlow.grainCount(); ++i)
                {
                    auto grain = discreteFlow.grainAt(i);

                    auto grainInfoBaseAddr = reinterpret_cast<std::uintptr_t>(discreteFlow.grainAt(i));
                    auto grainInfoSize = sizeof(GrainHeader);
                    auto grainPayloadSize = grain->header.info.grainSize;

                    // Host payloads follow the header in the grain file, so one contiguous region covers both.
                    regions.emplace_back(grainInfoBaseAddr, grainInfoSize + grainPayloadSize, nullptr, nullptr, Region::Location::host());
                }
            }

            auto const totalSlices = discreteFlow.grainAt(0)->header.info.totalSlices;
            return {std::move(regions),
                DataLayout::fromDiscrete(std::to_array(discreteFlow.flowInfo()->config.discrete.sliceSizes), totalSlices),
                discreteFlow.flowInfo()->config.common.maxSyncBatchSizeHint,
                regionsPerGrain};
        }
        else if (mxlIsContinuousDataFormat(static_cast<int>(flow.flowInfo()->config.common.format)))
        {
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
                continuousFlow.flowInfo()->config.common.maxSyncBatchSizeHint};
        }
        else
        {
            throw Exception::make(MXL_ERR_UNKNOWN, "Unsupported flow fromat {}", flow.flowInfo()->config.common.format);
        }
    }

    std::uint64_t getGrainIndexInRingSlot(std::vector<Region> const& regions, std::uint16_t slot, std::size_t regionsPerGrain)
    {
        auto const headerIndex = static_cast<std::size_t>(slot) * regionsPerGrain;
        if ((regionsPerGrain == 0) || (headerIndex >= regions.size()))
        {
            throw Exception::invalidArgument("Invalid ring buffer slot number: {}, ring buffer len: {}", slot, regions.size() / regionsPerGrain);
        }

        return *regions[headerIndex].grainIndexPtr;
    }

    void setValidSlicesForGrain(std::vector<Region> const& regions, std::uint16_t slot, std::uint16_t validSlices, std::size_t regionsPerGrain)
    {
        auto const headerIndex = static_cast<std::size_t>(slot) * regionsPerGrain;
        if ((regionsPerGrain == 0) || (headerIndex >= regions.size()))
        {
            throw Exception::invalidArgument("Invalid ring buffer slot number: {}, ring buffer len: {}", slot, regions.size() / regionsPerGrain);
        }

        *regions[headerIndex].validSlicesPtr = validSlices;
    }

    bool regionsNeedHmem(std::vector<Region> const& regions) noexcept
    {
        return std::ranges::any_of(regions, [](Region const& region) { return !region.loc.isHost(); });
    }
}
