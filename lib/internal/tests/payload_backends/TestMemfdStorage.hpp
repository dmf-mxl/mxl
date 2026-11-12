// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/**
 * \file TestMemfdStorage.hpp
 *
 * The storage type of the test-memfd backend and the meaning it gives to the reserved bytes of mxlGrainStorage. The
 * backend and the tests that read its flows share these definitions, as a backend outside the SDK and its applications
 * would.
 */

#include <cstdint>
#include <cstring>
#include <mxl/flow.h>

namespace mxl::tests::memfd
{
    /** The storage type of the test-memfd backend. */
    constexpr auto STORAGE_TYPE = "org.example.test-memfd";

    /** The part of a slot description the backend defines. All planes of a slot are in its one memory file. */
    struct SlotStorage
    {
        std::int32_t fd;        ///< The memory file that holds the slot, valid in the calling process.
        std::uint32_t reserved; ///< Zero.
        std::uint64_t fileSize; ///< The size of the memory file in bytes.
    };

    static_assert(sizeof(SlotStorage) <= sizeof(mxlGrainStorage::reserved));

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
