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
    ImmediateDataPool::ImmediateDataPool(std::size_t depth)
        : _buffers(depth == 0 ? 1 : depth)
    {}

    LocalRegion ImmediateDataPool::next() noexcept
    {
        auto const& buf = _buffers[_cursor];
        _cursor = (_cursor + 1) % _buffers.size();
        return buf.toLocalRegion();
    }

    std::size_t ImmediateDataPool::depth() const noexcept
    {
        return _buffers.size();
    }

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
            postReceives(endpoint, DefaultReceiveDepth);
        }
    }

    std::optional<Target::ReadResult> RMAGrainIngressProtocol::read(Endpoint const& endpoint, Completion const& completion)
    {
        auto completionData = completion.tryData();
        if (!completionData)
        {
            return {};
        }

        // One receive back for the one this completion consumed, so the
        // window stays open for as long as the connection lives.
        if (_immData)
        {
            static_cast<void>(tryPostOne(endpoint));
        }

        auto immData = completionData->data();
        if (!immData)
        {
            return {};
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

    void RMAGrainIngressProtocol::postReceives(Endpoint const& endpoint, std::size_t count)
    {
        if (!_immData)
        {
            _immData.emplace(count);
        }

        // Post as many as the queue will take, not as many as were asked
        // for. How deep it is was negotiated with the peer and can be
        // shallower than the window; a refusal is that limit being
        // reported, not a failure.
        //
        // Letting it throw would be worse than shallow. The caller runs
        // inside a visit over a state that has already been moved from,
        // so an exception leaves the state machine holding a moved-from
        // endpoint, and the next read dereferences a null one.
        auto posted = std::size_t{0};
        for (auto i = std::size_t{0}; i < _immData->depth(); ++i)
        {
            if (!tryPostOne(endpoint))
            {
                break;
            }
            ++posted;
        }

        if (posted < _immData->depth())
        {
            MXL_INFO("Receive queue accepted {} of {} receives for immediate data", posted, _immData->depth());
        }
    }

    bool RMAGrainIngressProtocol::tryPostOne(Endpoint const& endpoint)
    {
        try
        {
            endpoint.recv(_immData->next());
            return true;
        }
        catch (FabricException const& e)
        {
            MXL_DEBUG("Could not post a receive for immediate data: {}", e.what());
            return false;
        }
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
            postReceives(endpoint, DefaultReceiveDepth);
        }
    }

    std::optional<Target::ReadResult> RMASampleIngressProtocol::read(Endpoint const& endpoint, Completion const& completion)
    {
        auto completionData = completion.tryData();
        if (!completionData)
        {
            return {};
        }


        // One receive back for the one this completion consumed, so the
        // window stays open for as long as the connection lives.
        if (_immData)
        {
            static_cast<void>(tryPostOne(endpoint));
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

    void RMASampleIngressProtocol::postReceives(Endpoint const& endpoint, std::size_t count)
    {
        if (!_immData)
        {
            _immData.emplace(count);
        }

        // Post as many as the queue will take, not as many as were asked
        // for. How deep it is was negotiated with the peer and can be
        // shallower than the window; a refusal is that limit being
        // reported, not a failure.
        //
        // Letting it throw would be worse than shallow. The caller runs
        // inside a visit over a state that has already been moved from,
        // so an exception leaves the state machine holding a moved-from
        // endpoint, and the next read dereferences a null one.
        auto posted = std::size_t{0};
        for (auto i = std::size_t{0}; i < _immData->depth(); ++i)
        {
            if (!tryPostOne(endpoint))
            {
                break;
            }
            ++posted;
        }

        if (posted < _immData->depth())
        {
            MXL_INFO("Receive queue accepted {} of {} receives for immediate data", posted, _immData->depth());
        }
    }

    bool RMASampleIngressProtocol::tryPostOne(Endpoint const& endpoint)
    {
        try
        {
            endpoint.recv(_immData->next());
            return true;
        }
        catch (FabricException const& e)
        {
            MXL_DEBUG("Could not post a receive for immediate data: {}", e.what());
            return false;
        }
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
