#pragma once

#include <cstdint>

namespace mxl::lib::fabrics::ofi
{
    class SliceRange
    {
    public:
        /** \brief Create a SliceRange representing the range [start, end).
         *
         * \param start The inclusive start of the range.
         * \param end The exclusive end of the range.
         * \return A SliceRange representing the specified range.
         *
         * \throws Exception::invalidArgument if start is not less than end.
         */
        static SliceRange make(std::uint16_t start, std::uint16_t end);

        /** \brief Get the inclusive start of the range.
         */
        [[nodiscard]]
        std::uint16_t start() const noexcept;

        /** \brief Get the exclusive end of the range.
         */
        [[nodiscard]]
        std::uint16_t end() const noexcept;

        /** \brief Get the size of the range (end - start).
         * \note When start is 0, the size includes the header, it adds the payload offset.
         */
        [[nodiscard]]
        std::uint32_t transferSize(std::uint32_t payloadOffset, std::uint32_t sliceSize) const noexcept;

        /** \brief Get the offset within the payload for the start of the range.
         * \note When start is 0, the offset is 0, because we include the header in the transfer.
         */
        [[nodiscard]]
        std::uint32_t transferOffset(std::uint32_t payloadOffset, std::int32_t sliceSize) const noexcept;

    private:
        SliceRange(std::uint16_t start, std::uint16_t end) noexcept
            : _start(start)
            , _end(end)
        {}

    private:
        std::uint16_t _start; /**< Inclusive start of the range */
        std::uint16_t _end;   /**< Exclusive end of the range */
    };
}
