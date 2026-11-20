// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/**
 * \file TestMemfdStorage.hpp
 *
 * The storage type of the test-memfd backend, the meaning it gives to the reserved bytes of mxlGrainStorageLayout
 * and mxlGrainStorage, and the media type it defines. The backend and the tests that read its flows share these
 * definitions, as a backend outside the SDK and its applications would.
 */

#include <cstdint>
#include <cstring>
#include <mxl/flow.h>

namespace mxl::tests::memfd
{
    /** The storage type of the test-memfd backend. */
    constexpr auto STORAGE_TYPE = "org.example.test-memfd";

    /**
     * The media type the backend stores, which it defines: libmxl does not know it, and asks the backend for its
     * geometry. A made-up format of two planes: the first holds TWO_PLANE_FIRST_BYTES_PER_PIXEL bytes per pixel, the
     * second TWO_PLANE_SECOND_BYTES_PER_PIXEL. A slice is a line, and the frame width must be even.
     */
    constexpr auto MEDIA_TYPE = "video/x-test-two-plane";

    /** Bytes per pixel in the first plane of MEDIA_TYPE. */
    constexpr auto TWO_PLANE_FIRST_BYTES_PER_PIXEL = std::uint32_t{4};

    /** Bytes per pixel in the second plane of MEDIA_TYPE. */
    constexpr auto TWO_PLANE_SECOND_BYTES_PER_PIXEL = std::uint32_t{1};

    /**
     * Media types the backend claims to define but describes wrongly, so that the tests check that libmxl refuses the
     * answers: too many planes, a plane with a slice size of zero, no slices, and a grain that does not fit in 32 bits.
     */
    constexpr auto BAD_PLANES_MEDIA_TYPE = "video/x-test-bad-planes";
    constexpr auto BAD_SLICE_SIZE_MEDIA_TYPE = "video/x-test-bad-slice-size";
    constexpr auto BAD_SLICE_COUNT_MEDIA_TYPE = "video/x-test-bad-slice-count";
    constexpr auto HUGE_GRAIN_MEDIA_TYPE = "video/x-test-huge-grain";

    /** The part of the layout the backend defines. */
    struct Layout
    {
        char mediaType[32]; ///< The media type of the flow, null terminated.
    };

    /** The part of a slot description the backend defines. All planes of a slot are in its one memory file. */
    struct SlotStorage
    {
        std::int32_t fd;        ///< The memory file that holds the slot, valid in the calling process.
        std::uint32_t reserved; ///< Zero.
        std::uint64_t fileSize; ///< The size of the memory file in bytes.
    };

    static_assert(sizeof(Layout) <= sizeof(mxlGrainStorageLayout::reserved));
    static_assert(sizeof(SlotStorage) <= sizeof(mxlGrainStorage::reserved));

    /** \return The part of a layout the backend defines. */
    inline Layout layoutOf(mxlGrainStorageLayout const& layout) noexcept
    {
        auto result = Layout{};
        std::memcpy(&result, layout.reserved, sizeof(result));
        return result;
    }

    /** Store the part of a layout the backend defines. */
    inline void setLayout(mxlGrainStorageLayout& layout, Layout const& value) noexcept
    {
        std::memcpy(layout.reserved, &value, sizeof(value));
    }

    /** \return The part of a slot description the backend defines. */
    inline SlotStorage slotStorageOf(mxlGrainStorage const& storage) noexcept
    {
        auto result = SlotStorage{};
        std::memcpy(&result, storage.reserved, sizeof(result));
        return result;
    }

    /** Store the part of a slot description the backend defines. */
    inline void setSlotStorage(mxlGrainStorage& storage, SlotStorage const& value) noexcept
    {
        std::memcpy(storage.reserved, &value, sizeof(value));
    }
}
