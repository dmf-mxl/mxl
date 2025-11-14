#pragma once

#include <cstdint>
#include <utility>

namespace mxl::lib::fabrics::ofi
{

    /** \brief Immediate data representation for discrete flow transfers.
     */
    class ImmDataDiscrete
    {
    public:
        /** \brief Create immediate data from packed data.
         *
         * \param data The packed immediate data.
         */
        ImmDataDiscrete(std::uint32_t data) noexcept;

        /** \brief Create immediate data from ring buffer index and slice index.
         *
         * \param index The ring buffer index.
         * \param sliceIndex The slice index within the ring buffer.
         */
        ImmDataDiscrete(std::uint64_t index, std::uint16_t sliceIndex) noexcept;

        /** \brief Unpack the immediate data into ring buffer index and slice index.
         *
         * \return A pair containing the ring buffer index and slice index.
         */
        [[nodiscard]]
        std::pair<std::uint16_t, std::uint16_t> unpack() const noexcept;

        /** \brief Get the packed immediate data.
         *
         * \return The packed immediate data.
         */
        [[nodiscard]]
        std::uint32_t data() const noexcept;

    private:
        union
        {
            struct /**< Unpacked representation of immediate data. */
            {
                std::uint16_t ringBufferIndex;
                std::uint16_t sliceIndex;
            } packed;

            uint32_t data; /**< Packed representation of immediate data. */
        } _inner;
    };

}
