// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/**
 * \file TestMemfdStorage.hpp
 *
 * The storage type of the test-memfd backend and the meaning it gives to the reserved bytes of mxlGrainStorageLayout
 * and mxlGrainStorage. The backend and the tests that read its flows share these definitions, as a backend outside
 * the SDK and its applications would.
 */

#include <cstdint>
#include <cstring>
#include <mxl/flow.h>

namespace mxl::tests::memfd
{
    /** The storage type of the test-memfd backend. */
    constexpr auto STORAGE_TYPE = "org.example.test-memfd";

    /** The media type the backend stores. */
    constexpr auto MEDIA_TYPE = "video/v210";

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
