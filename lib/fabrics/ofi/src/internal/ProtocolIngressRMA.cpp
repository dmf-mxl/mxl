// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "ProtocolIngressRMA.hpp"
#include "mxl-internal/Logging.hpp"
#include "AudioBounceBuffer.hpp"
#include "DataLayout.hpp"
#include "Exception.hpp"
#include "ImmData.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{
    //
    // RMAGrainIngressProtocol implementations below
    RMAGrainIngressProtocol::RMAGrainIngressProtocol(std::vector<Region> regions)
        : _regions{std::move(regions)}
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

    std::optional<TargetInfoBounceBufferInfo> RMAGrainIngressProtocol::bounceBufferInfo() const
    {
        return std::nullopt;
    }

    void RMAGrainIngressProtocol::start(Endpoint const& endpoint)
    {
        if (endpoint.domain()->usingRecvBufForCqData())
        {
            endpoint.recv(immDataRegion());
        }
    }

    std::optional<Target::ReadResult> RMAGrainIngressProtocol::read(Endpoint const& endpoint, Completion const& completion)
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

    bool RMAGrainIngressProtocol::canReadGrains() const noexcept
    {
        return true;
    }

    bool RMAGrainIngressProtocol::canReadSamples() const noexcept
    {
        return false;
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
    RMASampleIngressProtocol::RMASampleIngressProtocol(Region region, DataLayout::Continuous const& layout, std::uint32_t maxSyncBatchSize)
        : _bounceBuffer{makeAudioBounceBuffer(layout, maxSyncBatchSize)}
        , _region{region}
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

    std::optional<TargetInfoBounceBufferInfo> RMASampleIngressProtocol::bounceBufferInfo() const
    {
        auto const entrySize = _bounceBuffer.entrySize(); // All entries have the same size, we can just take the size of the first one
        auto const entryCount = _bounceBuffer.entryCount();

        return TargetInfoBounceBufferInfo{.entryCount = entryCount, .entrySize = entrySize};
    }

    void RMASampleIngressProtocol::start(Endpoint const& endpoint)
    {
        if (endpoint.domain()->usingRecvBufForCqData())
        {
            endpoint.recv(immDataRegion());
        }
    }

    std::optional<Target::ReadResult> RMASampleIngressProtocol::read(Endpoint const& endpoint, Completion const& completion)
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

        auto const immData = completionData->data();
        if (!immData)
        {
            throw Exception::invalidState("Received a completion without immediate data.");
        }

        auto const header = _bounceBuffer.unpack(*immData, _region);
        return std::make_optional<Target::SampleReadResult>(header.headIndex, header.count);
    }

    bool RMASampleIngressProtocol::canReadGrains() const noexcept
    {
        return false;
    }

    bool RMASampleIngressProtocol::canReadSamples() const noexcept
    {
        return true;
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

    AudioBounceBuffer RMASampleIngressProtocol::makeAudioBounceBuffer(DataLayout::Continuous const& layout, std::uint32_t maxSyncBatchSize)
    {
        auto const oneSampleSize = layout.sampleSize * layout.channelCount;
        auto const entrySize = oneSampleSize * maxSyncBatchSize;
        auto const historySize = layout.bufferLength * oneSampleSize;

        auto const entryCount = (historySize + entrySize - 1U) / entrySize; // ceil(historySize / entrySize)

        MXL_INFO("Creating audio bounce buffer with entry size {} bytes and entry count {}, maxSyncBatchSize {} historySize {} bufferLength {}",
            entrySize,
            entryCount,
            maxSyncBatchSize,
            historySize,
            layout.bufferLength);
        return {entryCount, entrySize, layout};
    }

}
