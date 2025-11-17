#include "DataLayout.hpp"
#include <cassert>
#include <array>
#include "mxl/flowinfo.h"

namespace mxl::lib::fabrics::ofi
{
    DataLayout DataLayout::fromVideo(std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> sliceSizes) noexcept
    {
        return DataLayout{DataLayout::VideoDataLayout{.sliceSizes = sliceSizes}};
    };

    bool DataLayout::isVideo() const noexcept
    {
        return std::holds_alternative<VideoDataLayout>(_inner);
    }

    DataLayout::VideoDataLayout const& DataLayout::asVideo() const noexcept
    {
        return std::get<VideoDataLayout>(_inner);
    }

    DataLayout::DataLayout(InnerLayout inner) noexcept
        : _inner(inner)
    {}
}
