// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ProtocolIngressRMA.hpp"
#include "AudioBounceBuffer.hpp"
#include "DataLayout.hpp"
#include "Exception.hpp"
#include "ImmData.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{

    namespace
    {
        /** \brief Helper function to create an AudioBounceBuffer based on the given audio data layout and maximum synchronous batch size.
         * Entries are as big as the maximum number of samples that can be transferred in a single batch, which is determined by maxSyncBatchSize.
         * The number of entries is determined by how many batches are needed to cover the entire history buffer.
         */
        AudioBounceBuffer makeAudioBounceBuffer(DataLayout::AudioDataLayout const& layout, std::uint32_t maxSyncBatchSize)
        {
            auto oneSampleSize = layout.sampleSize * layout.channelCount;
            auto entrySize = oneSampleSize * maxSyncBatchSize;
            auto historySize = layout.bufferLength * oneSampleSize;

            auto entryCount = (historySize + entrySize - 1) / entrySize; // ceil(historySize / entrySize)

            return {entryCount, entrySize, layout};
        }

    }

    //
    // RMAGrainIngressProtocol implementations below
    RMAGrainIngressProtocol::RMAGrainIngressProtocol(std::vector<Region> regions)
        : _regions(std::move(regions))
    {}

    std::vector<RemoteRegion> RMAGrainIngressProtocol::registerMemory(std::shared_ptr<Domain> domain)
    {
        if (_isMemoryRegistered)
        {
            throw Exception::invalidState("Memory is already registered.");
        }

        domain->registerRegions(_regions, FI_REMOTE_WRITE);
        _isMemoryRegistered = true;

        return domain->remoteRegions();
    }

    std::optional<TargetInfoBounceBufferInfo> RMAGrainIngressProtocol::bounceBufferInfo()
    {
        return std::nullopt;
    }

    void RMAGrainIngressProtocol::start(Endpoint& endpoint)
    {
        if (endpoint.domain()->usingRecvBufForCqData())
        {
            endpoint.recv(immDataRegion());
        }
    }

    std::optional<Target::ReadResult> RMAGrainIngressProtocol::read(Endpoint& endpoint, Completion const& completion)
    {
        auto completionData = completion.tryData();
        if (!completionData)
        {
            return {};
        }

        if (_immDataBuffer)
        {
            endpoint.recv(_immDataBuffer->toLocalRegion());
        }

        auto immData = completionData->data();
        if (!immData)
        {
            throw Exception::invalidState("Received a completion without immediate data.");
        }

        auto [slot, slice] = ImmDataGrain{static_cast<std::uint32_t>(*immData)}.unpack();

        // Set the number of valid slices in the grain header. This information is received through the immediate data and must be updated
        // in the local shared memory in the case of partial writes.
        setValidSlicesForGrain(_regions, slot, slice);

        // Get the actual grain index from the grain header in share memory. This was written in the first RMA write.
        auto grainIndex = getGrainIndexInRingSlot(_regions, slot);

        return std::make_optional<Target::GrainReadResult>(grainIndex);
    }

    void RMAGrainIngressProtocol::reset()
    {}

    LocalRegion RMAGrainIngressProtocol::immDataRegion()
    {
        if (!_immDataBuffer)
        {
            _immDataBuffer.emplace();
        }

        return _immDataBuffer->toLocalRegion();
    }

    //
    // RMASampleIngressProtocol implementations below
    RMASampleIngressProtocol::RMASampleIngressProtocol(Region region, DataLayout::AudioDataLayout const& layout, std::uint32_t maxSyncBatchSize)
        : _bounceBuffer(makeAudioBounceBuffer(layout, maxSyncBatchSize))
        , _region(region)
    {}

    std::vector<RemoteRegion> RMASampleIngressProtocol::registerMemory(std::shared_ptr<Domain> domain)
    {
        if (_isMemoryRegistered)
        {
            throw Exception::invalidState("Memory is already registered.");
        }

        domain->registerRegions(_bounceBuffer.getRegions(), FI_REMOTE_WRITE);
        _isMemoryRegistered = true;

        return domain->remoteRegions();
    }

    std::optional<TargetInfoBounceBufferInfo> RMASampleIngressProtocol::bounceBufferInfo()
    {
        auto entrySize = static_cast<std::uint32_t>(
            _bounceBuffer.getRegions().front().size); // All entries have the same size, we can just take the size of the first one
        auto entryCount = _bounceBuffer.nbEntries();

        return TargetInfoBounceBufferInfo{.entryCount = entryCount, .entrySize = entrySize};
    }

    void RMASampleIngressProtocol::start(Endpoint& endpoint)
    {
        if (endpoint.domain()->usingRecvBufForCqData())
        {
            endpoint.recv(immDataRegion());
        }
    }

    std::optional<Target::ReadResult> RMASampleIngressProtocol::read(Endpoint& endpoint, Completion const& completion)
    {
        auto completionData = completion.tryData();
        if (!completionData)
        {
            return {};
        }

        if (_immDataBuffer)
        {
            endpoint.recv(immDataRegion());
        }

        auto immData = completionData->data();
        if (!immData)
        {
            throw Exception::invalidState("Received a completion without immediate data.");
        }

        auto entryIndex = ImmDataSample{static_cast<std::uint32_t>(*immData)}.unpack().bounceBufferEntry;

        auto header = _bounceBuffer.unpack(entryIndex, _region);

        return std::make_optional<Target::SampleReadResult>(header.headIndex, header.count);
    }

    void RMASampleIngressProtocol::reset()
    {}

    LocalRegion RMASampleIngressProtocol::immDataRegion()
    {
        if (!_immDataBuffer)
        {
            _immDataBuffer.emplace();
        }

        return _immDataBuffer->toLocalRegion();
    }

}
