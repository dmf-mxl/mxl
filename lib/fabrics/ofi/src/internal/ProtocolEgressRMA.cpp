#include "ProtocolEgressRMA.hpp"
#include "Exception.hpp"
#include "ImmData.hpp"

namespace mxl::lib::fabrics::ofi
{
    RMAGrainEgressProtocol::RMAGrainEgressProtocol(Completion::Token token, TargetInfo info, DataLayout layout, std::vector<LocalRegion> localRegions)
        : _token(token)
        , _remoteInfo(std::move(info))
        , _layout(std::move(layout))
        , _localRegions(std::move(localRegions))
    {}

    void RMAGrainEgressProtocol::transferGrain(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
        SliceRange const& sliceRange, ::fi_addr_t destAddr)
    {
        auto layout = _layout.asVideo();

        auto localSize = sliceRange.transferSize(payloadOffset, layout.sliceSizes[0]);
        auto localOffset = sliceRange.transferOffset(payloadOffset, layout.sliceSizes[0]);
        auto remoteSize = sliceRange.transferSize(payloadOffset, layout.sliceSizes[0]);
        auto remoteOffset = sliceRange.transferOffset(payloadOffset, layout.sliceSizes[0]);

        auto localRegion = _localRegions[localIndex % _localRegions.size()].sub(localOffset, localSize);
        auto remoteRegion = _remoteInfo.remoteRegions[remoteIndex % _remoteInfo.remoteRegions.size()].sub(remoteOffset, remoteSize);

        _pending += ep.write(_token, localRegion, remoteRegion, destAddr, ImmDataGrain{remoteIndex, sliceRange.end()}.data());
    }

    void RMAGrainEgressProtocol::processCompletion(Completion::Data const&)
    {
        --_pending;
    }

    bool RMAGrainEgressProtocol::hasPendingWork() const
    {
        return _pending > 0;
    }

    std::size_t RMAGrainEgressProtocol::reset()
    {
        return std::exchange(_pending, 0);
    }

    RMAGrainEgressProtocolTemplate::RMAGrainEgressProtocolTemplate(DataLayout layout, std::vector<Region> regions)
        : _dataLayout(std::move(layout))
        , _regions(std::move(regions))
    {}

    void RMAGrainEgressProtocolTemplate::registerMemory(std::shared_ptr<Domain> domain)
    {
        if (_localRegions)
        {
            throw Exception::invalidState("Memory already registered.");
        }

        domain->registerRegions(_regions, FI_WRITE);
        _localRegions = domain->localRegions();
    }

    std::unique_ptr<EgressProtocol> RMAGrainEgressProtocolTemplate::createInstance(Completion::Token token, TargetInfo remoteInfo)
    {
        if (!_localRegions)
        {
            throw Exception::invalidState("Cannot create protocol before memory is registered.");
        }

        struct MakeUniqueEnabler : RMAGrainEgressProtocol
        {
            MakeUniqueEnabler(Completion::Token token, TargetInfo info, DataLayout layout, std::vector<LocalRegion> localRegion)
                : RMAGrainEgressProtocol(token, std::move(info), std::move(layout), std::move(localRegion))
            {}
        };

        return std::make_unique<MakeUniqueEnabler>(token, std::move(remoteInfo), _dataLayout, *_localRegions);
    }

}
