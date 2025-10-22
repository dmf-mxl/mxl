#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace mxl::lib::fabrics::ofi
{

    class ImmDataGrain
    {
    public:
        ImmDataGrain(std::uint32_t data) noexcept;
        ImmDataGrain(std::uint64_t grainIndex, std::uint16_t sliceIndex) noexcept;

        [[nodiscard]]
        std::pair<std::uint64_t, std::uint16_t> unpack() const noexcept;
        [[nodiscard]]
        std::uint32_t data() const noexcept;

    private:
        union
        {
            struct
            {
                std::uint16_t grainIndex;
                std::uint16_t sliceIndex;
            };

            uint32_t data;
        } _inner;
    };

    class ImmDataSample
    {
    public:
        ImmDataSample(std::uint32_t data) noexcept;
        ImmDataSample(std::uint64_t headIndex, std::size_t count) noexcept;

        [[nodiscard]]
        std::pair<std::uint64_t, std::size_t> unpack() const noexcept;
        [[nodiscard]]
        std::uint32_t data() const noexcept;

    private:
        union
        {
            struct
            {
                std::uint16_t headIndex;
                std::uint16_t count;
            };

            uint32_t data;
        } _inner;
    };
}
