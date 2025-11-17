#include "ProtocolEgress.hpp"
#include <cstddef>
#include <utility>
#include "ImmData.hpp"
#include "GrainSlices.hpp"

namespace mxl::lib::fabrics::ofi
{
    EgressProtocolWriter::EgressProtocolWriter(std::shared_ptr<Endpoint> ep, std::vector<RemoteRegion> const& remoteRegions,
        DataLayout::VideoDataLayout const& layout)

        : _ep(std::move(ep))
        , _remoteRegions(remoteRegions)
        , _layout(layout)
    {}

    std::size_t EgressProtocolWriter::transferGrain(LocalRegion const& localRegion, std::uint64_t remoteIndex, std::uint32_t remotePayloadOffset,
        SliceRange const& sliceRange)
    {
        auto ringIndex = remoteIndex % _remoteRegions.size();
        auto offset = sliceRange.transferOffset(remotePayloadOffset, _layout.sliceSizes[0]);
        auto size = sliceRange.transferSize(remotePayloadOffset, _layout.sliceSizes[0]);
        auto const& remoteRegion = _remoteRegions[ringIndex].sub(offset, size);

        return _ep->write(localRegion, remoteRegion, FI_ADDR_UNSPEC, ImmDataGrain{ringIndex, sliceRange.end()}.data());
    }
}
