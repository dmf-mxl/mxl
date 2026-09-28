// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PayloadStorage.hpp"
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <fmt/format.h>
#include "mxl-internal/DiscreteFlowData.hpp"

namespace mxl::lib
{
    namespace
    {
        /**
         * Create a slot description with the fields common to all storage types set.
         * \param[in] type The storage type of the slot.
         * \param[in] slot The slot index.
         * \return A slot description with version, size, storage type and slot set, and all other bytes zero.
         */
        mxlGrainStorage makeSlotStorageHeader(mxlPayloadStorageType type, std::size_t slot) noexcept
        {
            auto result = mxlGrainStorage{};
            // Value initialization only initializes the first union member. Zero the whole structure so that
            // the reserved bytes are defined and two descriptions of the same slot compare equal.
            std::memset(&result, 0, sizeof(result));
            result.version = 1U;
            result.size = sizeof(mxlGrainStorage);
            result.storageType = type;
            result.slot = static_cast<std::uint32_t>(slot);
            return result;
        }
    }

    // The storage descriptors are public and must keep their size once released. Their reserved space is where
    // later storage types, such as dma-buf or exported images, add their fields.
    static_assert(sizeof(mxlGrainPlaneLayout) == 32, "mxlGrainPlaneLayout must be 32 bytes.");
    static_assert(sizeof(mxlGrainStorageLayout) == 512, "mxlGrainStorageLayout must be 512 bytes.");
    static_assert(sizeof(mxlGrainStorage) == 256, "mxlGrainStorage must be 256 bytes.");
    static_assert(std::has_unique_object_representations_v<mxlGrainStorageLayout>, "mxlGrainStorageLayout must not contain padding.");

    PayloadStorage::~PayloadStorage() = default;

    HostPayloadStorage::HostPayloadStorage(DiscreteFlowData& flowData)
        : _slotPayloads{}
    {
        auto const count = flowData.grainCount();
        _slotPayloads.reserve(count);
        for (auto slot = std::size_t{0}; slot < count; ++slot)
        {
            // The payload directly follows the grain header in the grain segment.
            _slotPayloads.push_back(&flowData.grainAt(slot)->header + 1);
        }
    }

    mxlPayloadStorageType HostPayloadStorage::type() const noexcept
    {
        return MXL_PAYLOAD_STORAGE_HOST_POINTER;
    }

    std::int32_t HostPayloadStorage::deviceIndex() const noexcept
    {
        return -1;
    }

    std::size_t HostPayloadStorage::slotCount() const noexcept
    {
        return _slotPayloads.size();
    }

    mxlGrainStorage HostPayloadStorage::slotStorage(std::size_t slot) const
    {
        if (slot >= _slotPayloads.size())
        {
            throw std::out_of_range{fmt::format("Slot {} is out of range, the storage has {} slots.", slot, _slotPayloads.size())};
        }

        auto result = makeSlotStorageHeader(type(), slot);
        result.host.pointer = _slotPayloads[slot];
        return result;
    }

    void HostPayloadStorage::describeLayout(mxlGrainStorageLayout& layout) const
    {
        layout.storageType = type();
        layout.deviceIndex = deviceIndex();
        std::memset(layout.deviceUuid, 0, sizeof(layout.deviceUuid));
    }

    void HostPayloadStorage::mapSlots() const
    {}

    mxlGrainStorageLayout makeGrainStorageLayout(mxlDiscreteFlowConfigInfo const& config, std::uint16_t totalSlices, PayloadStorage const& storage)
    {
        auto result = mxlGrainStorageLayout{};
        result.version = 1U;
        result.size = sizeof(mxlGrainStorageLayout);
        result.deviceIndex = -1;
        result.slotCount = static_cast<std::uint32_t>(storage.slotCount());

        // The planes in use are the leading entries with a non-zero slice size. By default they follow each
        // other without padding between slices.
        auto offset = std::uint64_t{0};
        for (auto const sliceSize : config.sliceSizes)
        {
            if (sliceSize == 0U)
            {
                break;
            }

            auto& plane = result.planes[result.planeCount];
            plane.offset = offset;
            plane.pitch = sliceSize;
            plane.size = std::uint64_t{sliceSize} * totalSlices;
            offset += plane.size;
            ++result.planeCount;
        }

        auto const defaults = result;
        storage.describeLayout(result);

        // The storage may only change the fields it owns.
        if ((result.version != defaults.version) || (result.size != defaults.size) || (result.slotCount != defaults.slotCount) ||
            (result.planeCount != defaults.planeCount) || (result.storageType != storage.type()))
        {
            throw std::logic_error{"Payload storage changed fields of the storage layout that it does not own."};
        }

        // Every plane must be able to hold all its slices at the reported pitch.
        for (auto i = std::uint32_t{0}; i < result.planeCount; ++i)
        {
            auto const& plane = result.planes[i];
            auto const sliceSize = std::uint64_t{config.sliceSizes[i]};
            auto const requiredSize = (totalSlices > 0U) ? ((plane.pitch * (totalSlices - 1U)) + sliceSize) : std::uint64_t{0};
            if ((plane.pitch < sliceSize) || (plane.size < requiredSize) || (plane.reserved != 0U))
            {
                throw std::logic_error{fmt::format("Payload storage described plane {} with a pitch of {} bytes and a size of {} bytes, "
                                                   "which cannot hold {} slices of {} bytes.",
                    i,
                    plane.pitch,
                    plane.size,
                    totalSlices,
                    sliceSize)};
            }
        }

        return result;
    }
}
