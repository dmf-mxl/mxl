#include "ImmData.hpp"
#include <cstddef>
#include <cstdint>

namespace mxl::lib::fabrics::ofi
{
    ImmDataGrain::ImmDataGrain(std::uint32_t data) noexcept
    {
        _inner.data = data;
    }

    ImmDataGrain::ImmDataGrain(std::uint64_t grainIndex, std::uint16_t sliceIndex) noexcept
    {
        _inner.grainIndex = grainIndex;
        _inner.sliceIndex = sliceIndex;
    }

    std::pair<std::uint64_t, std::uint16_t> ImmDataGrain::unpack() const noexcept
    {
        return {_inner.grainIndex, _inner.sliceIndex};
    }

    std::uint32_t ImmDataGrain::data() const noexcept
    {
        return _inner.data;
    }

    ImmDataSample::ImmDataSample(std::uint32_t data) noexcept
    {
        _inner.data = data;
    }

    ImmDataSample::ImmDataSample(std::uint64_t headIndex, std::size_t count) noexcept
    {
        _inner.headIndex = headIndex;
        _inner.count = count;
    }

    std::pair<std::uint64_t, std::size_t> ImmDataSample::unpack() const noexcept
    {
        return {_inner.headIndex, _inner.count};
    }

    std::uint32_t ImmDataSample::data() const noexcept
    {
        return _inner.data;
    }

}
