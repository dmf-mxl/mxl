// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ProtocolEgressRMA.hpp"
#include "AudioBounceBuffer.hpp"
#include "DataLayout.hpp"
#include "Exception.hpp"
#include "ImmData.hpp"
#include "LocalRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    RMAGrainEgressProtocol::RMAGrainEgressProtocol(Completion::Token token, TargetInfo info, DataLayout::Discrete layout,
        std::vector<LocalRegion> localRegions)
        : _token{token}
        , _remoteInfo{std::move(info)}
        , _layout{layout}
        , _localRegions{std::move(localRegions)}
    {}

    void RMAGrainEgressProtocol::registerMemory(std::shared_ptr<Domain>)
    {
        // For this protocol, memory registration is completely handled at the protocol template level, so this function doesn't need to do anything.
    }

    void RMAGrainEgressProtocol::transferGrain(Endpoint const& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
        SliceRange const& sliceRange, ::fi_addr_t destAddr)
    {
        auto const localSize = sliceRange.transferSize(payloadOffset, _layout.sliceSizes[0]);
        auto const localOffset = sliceRange.transferOffset(payloadOffset, _layout.sliceSizes[0]);
        auto const remoteSize = sliceRange.transferSize(payloadOffset, _layout.sliceSizes[0]);
        auto const remoteOffset = sliceRange.transferOffset(payloadOffset, _layout.sliceSizes[0]);

        auto const localRegion = _localRegions[localIndex % _localRegions.size()].sub(localOffset, localSize);
        auto const remoteRegion = _remoteInfo.remoteRegions[remoteIndex % _remoteInfo.remoteRegions.size()].sub(remoteOffset, remoteSize);
        auto const remoteSlot = remoteIndex % _remoteInfo.remoteRegions.size();

        _pending += ep.write(_token, localRegion, remoteRegion, destAddr, ImmDataGrain{remoteSlot, sliceRange.end()}.data());
    }

    void RMAGrainEgressProtocol::transferSamples(Endpoint const&, std::uint64_t, std::size_t, ::fi_addr_t)
    {
        throw Exception::invalidState("transferSamples is not supported in RMAGrainEgressProtocol.");
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

    RMAGrainEgressProtocolTemplate::RMAGrainEgressProtocolTemplate(DataLayout::Discrete layout, std::vector<Region> regions)
        : _layout{layout}
        , _regions{std::move(regions)}
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
            MakeUniqueEnabler(Completion::Token token, TargetInfo info, DataLayout::Discrete layout, std::vector<LocalRegion> localRegion)
                : RMAGrainEgressProtocol{token, std::move(info), layout, std::move(localRegion)}
            {}
        };

        return std::make_unique<MakeUniqueEnabler>(token, std::move(remoteInfo), _layout, *_localRegions);
    }

    RMASampleEgressProtocol::RMASampleEgressProtocol(Completion::Token token, TargetInfo info, DataLayout::Continuous layout, LocalRegion localRegion,
        std::size_t bounceBufferEntryCount)
        : _token{token}
        , _remoteInfo{std::move(info)}
        , _layout{layout}
        , _localRegion{localRegion}
        , _entryHeaders{bounceBufferEntryCount}
        , _bounceBufferEntryCount{bounceBufferEntryCount}
    {}

    void RMASampleEgressProtocol::registerMemory(std::shared_ptr<Domain> domain)
    {
        if (_entryHeaders.empty())
        {
            throw Exception::invalidState("Entry headers buffer are not initialized.");
        }

        auto entryHeaderRegions = std::vector<Region>{};
        entryHeaderRegions.reserve(_entryHeaders.size());
        for (auto& header : _entryHeaders)
        {
            entryHeaderRegions.emplace_back(reinterpret_cast<std::uintptr_t>(&header),
                sizeof(AudioEntryHeader),
                nullptr,
                nullptr,
                Region::Location::host()); // Host, because the bounce buffer will always be stored on host memory.
        }

        // The domain is shared with all endpoints, if there's more than 1 target, we can't assume that the first audio header entry for this target
        // will be at index 1 (right after the audio samples buffer). Read the number of regions already registered to the domain, then
        // register the entry header regions, and finally get the local regions again to retrieve the actual regions corresponding to the entry
        // headers we just registered. Use the offset to correctly get the entry header regions in the list of local regions.
        auto const offset = domain->localRegions().size();
        domain->registerRegions(entryHeaderRegions, FI_WRITE);
        auto const localRegions = domain->localRegions();

        _entryHeaderRegions = std::vector(localRegions.cbegin() + static_cast<std::ptrdiff_t>(offset), localRegions.cend());
    }

    void RMASampleEgressProtocol::transferGrain(Endpoint const&, std::uint64_t, std::uint64_t, std::uint32_t, SliceRange const&, ::fi_addr_t)
    {
        throw Exception::invalidState("transferGrain is not supported in RMASampleEgressProtocol.");
    }

    void RMASampleEgressProtocol::transferSamples(Endpoint const& ep, std::uint64_t headIndex, std::size_t count, ::fi_addr_t destAddr)
    {
        if (count == 0)
        {
            throw Exception::invalidArgument("Count must be greater than 0.");
        }

        auto const entrySizeRequired = (_layout.sampleSize * _layout.channelCount * count) + sizeof(AudioEntryHeader);
        if (entrySizeRequired > _remoteInfo.bounceBufferInfo->entrySize)
        {
            throw Exception::invalidArgument("Count is too large for the bounce buffer entry size. Count {}, entry size {}, required entry size {}.",
                count,
                _remoteInfo.bounceBufferInfo->entrySize,
                entrySizeRequired);
        }

        // 1- Create the scatter-gather list for the transfer and prepend the audio header.
        auto sgl = makeScatterGatherList(_layout, headIndex, count, _localRegion);
        // set the header and prepend it to the scatter-gather list.
        _entryHeaders[_bounceBufferEntryIndex].headIndex = headIndex;
        _entryHeaders[_bounceBufferEntryIndex].count = count;
        sgl.insert(sgl.begin(), _entryHeaderRegions[_bounceBufferEntryIndex]);

        // 2- Get the remote region
        auto const remoteRegion = _remoteInfo.remoteRegions[_bounceBufferEntryIndex % _remoteInfo.remoteRegions.size()];

        // 3- Send the remote write
        _pending += ep.write(_token, sgl, remoteRegion, destAddr, _bounceBufferEntryIndex);

        // 4- update bounce buffer entry index for the next transfer
        _bounceBufferEntryIndex = (_bounceBufferEntryIndex + 1) % _bounceBufferEntryCount;
    }

    void RMASampleEgressProtocol::processCompletion(Completion::Data const&)
    {
        --_pending;
    }

    bool RMASampleEgressProtocol::hasPendingWork() const
    {
        return _pending > 0;
    }

    std::size_t RMASampleEgressProtocol::reset()
    {
        return std::exchange(_pending, 0);
    }

    std::vector<LocalRegion> RMASampleEgressProtocol::makeScatterGatherList(DataLayout::Continuous const& layout, std::uint64_t headIndex,
        std::size_t count, LocalRegion const& region)
    {
        auto slice = mxlMutableWrappedMultiBufferSlice{};
        AudioBounceBuffer::getMutableMultiBufferSlices(headIndex,
            count,
            layout.bufferLength,
            layout.sampleSize,
            layout.channelCount,
            reinterpret_cast<std::uint8_t*>(region.addr), // NOLINT
            slice);

        // Double the scatter-gather list length if the second fragment is present.
        auto const sgListLen = (slice.base.fragments[1].size > 0) ? 2 * slice.count : slice.count;

        // Create the scatter-gather list using the slices. We create at least one scatter-gather entry per channel. We potentially create an
        // additional one per channel if 2 fragments are present (wrap-around). When a fragment is not present its size will be 0.
        auto sgList = std::vector<LocalRegion>{};
        sgList.reserve(sgListLen);
        // Write all fragments for one channel before moving to the next channel.
        // Doing otherwise (fragment first) only works when source and target wrapped slices split at the same fragment sizes.
        // If source and target have different ring positions, the target consumes the bytes with different fragment sizes and "stitches" the channel
        // data back together incorrectly.
        for (auto chan = std::size_t{0}; chan < slice.count; chan++)
        {
            for (auto const& fragment : slice.base.fragments)
            {
                // check if the fragment present
                if (fragment.size > 0)
                {
                    auto const srcAddr = reinterpret_cast<std::uintptr_t>(fragment.pointer) + (slice.stride * chan);

                    sgList.emplace_back(LocalRegion{.addr = srcAddr, .len = fragment.size, .desc = region.desc});
                }
            }
        }

        return sgList;
    }

    RMASampleEgressProtocolTemplate::RMASampleEgressProtocolTemplate(DataLayout::Continuous layout, Region region)
        : _layout{layout}
        , _region{region}
    {}

    void RMASampleEgressProtocolTemplate::registerMemory(std::shared_ptr<Domain> domain)
    {
        // This function should be called once during setup and be the first memory to register, otherwise it's a bug.
        if (_localRegion)
        {
            throw Exception::invalidState("Memory already registered.");
        }
        if (!domain->localRegions().empty())
        {
            throw Exception::invalidState("No memory should be previously registered.");
        }

        // Register the audio region provided by the user. When the actual protocol instance is created, the protocol will register additional
        // regions for the bounce buffer entry headers, but we can only do that once we have the actual protocol instance since the bounce buffer
        // entry count is a parameter of the protocol instance.
        domain->registerRegion(_region, FI_WRITE);

        _localRegion = domain->localRegions().front();
    }

    std::unique_ptr<EgressProtocol> RMASampleEgressProtocolTemplate::createInstance(Completion::Token token, TargetInfo remoteInfo)
    {
        if (!_localRegion)
        {
            throw Exception::invalidState("Cannot create protocol before memory is registered.");
        }

        if (!remoteInfo.bounceBufferInfo)
        {
            throw Exception::invalidArgument("Remote target does not have bounce buffer info required for sample egress protocol.");
        }

        struct MakeUniqueEnabler : RMASampleEgressProtocol
        {
            MakeUniqueEnabler(Completion::Token token, TargetInfo info, DataLayout::Continuous layout, LocalRegion localRegion,
                std::uint32_t bounceBufferEntryCount)
                : RMASampleEgressProtocol{token, std::move(info), layout, localRegion, bounceBufferEntryCount}
            {}
        };

        return std::make_unique<MakeUniqueEnabler>(token, std::move(remoteInfo), _layout, *_localRegion, remoteInfo.bounceBufferInfo->entryCount);
    };
}
