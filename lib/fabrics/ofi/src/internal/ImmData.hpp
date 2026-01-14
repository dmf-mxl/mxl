// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace mxl::lib::fabrics::ofi
{

    /** \brief Immediate data representation for discrete flow transfers.
     */
    class ImmDataGrain
    {
    public:
        struct Unpacked //**< Unpacked representation of immediate data. */
        {
            std::uint16_t ringBufferIndex;
            std::uint16_t sliceIndex;
        };

    public:
        /** \brief Create immediate data from packed data.
         *
         * \param data The packed immediate data.
         */
        ImmDataGrain(std::uint32_t data) noexcept;

        /** \brief Create immediate data from ring buffer index and slice index.
         *
         * \param index The ring buffer index.
         * \param sliceIndex The slice index within the ring buffer.
         */
        ImmDataGrain(std::uint64_t index, std::uint16_t sliceIndex) noexcept;

        /** \brief Unpack the immediate data into ring buffer index and slice index.
         *
         * \return A pair containing the ring buffer index and slice index.
         */
        [[nodiscard]]
        Unpacked unpack() const noexcept;

        /** \brief Get the packed immediate data.
         *
         * \return The packed immediate data.
         */
        [[nodiscard]]
        std::uint32_t data() const noexcept;

    private:
        uint32_t _inner; /**< Packed representation of immediate data. */
    };

}
