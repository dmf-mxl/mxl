// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>

#include "AudioBounceBuffer.hpp"
#include "DataLayout.hpp"
#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{
    /** \brief A fixed pool of landing buffers for immediate data, with a
     * round-robin cursor over them.
     *
     * A provider that reports FI_RX_CQ_DATA delivers the immediate data of
     * an RDMA write with immediate only into a posted receive, and the
     * write consumes that receive. Keeping a single one posted therefore
     * lets an initiator have exactly one write in flight: the second write
     * of any burst finds the receive queue empty and the transport falls
     * back on RNR retry. No credit is exchanged that would tell the
     * initiator to stop, so it keeps retrying -- on providers that leave
     * the RNR retry count at its infinite sentinel, forever.
     *
     * Keeping a window posted is what lets a writer committing several
     * batches between two transfer periods be mirrored at all. The buffers
     * themselves are never read: the immediate data is taken from the
     * completion, and they exist only because the provider requires
     * somewhere to put it.
     *
     * The storage is sized once and never resized, because a posted
     * receive holds the address of its buffer.
     */
    class ImmediateDataPool
    {
    public:
        explicit ImmediateDataPool(std::size_t depth);

        /** \brief The next buffer in the pool, as a region to post.
         */
        [[nodiscard]]
        LocalRegion next() noexcept;

        /** \brief How many receives are meant to be kept posted against it.
         */
        [[nodiscard]]
        std::size_t depth() const noexcept;

    private:
        std::vector<Target::ImmediateDataLocation> _buffers;
        std::size_t _cursor{0};
    };

    /** \brief Receives kept posted for immediate data.
     *
     * Large enough that a producer committing several batches between two
     * transfer periods is absorbed without an RNR round trip, and far below
     * the receive-queue size every provider we build against reports.
     */
    constexpr std::size_t DefaultReceiveDepth = 32;

    /** \brief Ingress protocol for RMA writer endpoint.
     *
     * Handles processing of completions when paired with an endpoint that does remote write to our buffers without bounce buffering.
     */
    class RMAGrainIngressProtocol final : public IngressProtocol
    {
    public:
        RMAGrainIngressProtocol(std::vector<Region> regions);

        /** \copydoc IngressProtocol::registerMemory()
         */
        [[nodiscard]]
        virtual std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc IngressProtocol::bounceBufferInfo()
         *\note This protocol does not use a bounce buffer, so this function returns an empty optional.
         */
        [[nodiscard]]
        virtual std::optional<TargetInfoBounceBufferInfo> bounceBufferInfo() const override;

        /** \copydoc IngressProtocol::start()
         */
        virtual void start(Endpoint const& endpoint) override;

        /** \copydoc IngressProtocol::processCompletion()
         */
        [[nodiscard]]
        virtual std::optional<Target::ReadResult> read(Endpoint const& endpoint, Completion const& completion) override;

        /**\brief This protocol can read grains, but not samples, since it is designed for remote writes of grain buffers.
         */
        [[nodiscard]]
        virtual bool canReadGrains() const noexcept override;

        /** \brief This protocol cannot read samples, since it is designed for remote writes of grain buffers.
         */
        [[nodiscard]]
        virtual bool canReadSamples() const noexcept override;

        /** \copydoc IngressProtocol::destroy()
         */
        virtual void reset() override;

    private:
        void postReceives(Endpoint const& endpoint, std::size_t count);
        bool tryPostOne(Endpoint const& endpoint);

    private:
        std::vector<Region> _regions;
        bool _isMemoryRegistered{false};
        std::optional<ImmediateDataPool> _immData{};
    };

    /** \brief Ingress protocol for RMA writer endpoint for audio samples.
     */
    class RMASampleIngressProtocol final : public IngressProtocol
    {
    public:
        /** Construct an RMASampleIngressProtocol with the given region and data layout.
         * \param region The memory region containing audio. The audio samples will be first received in one of the bounce buffer entry and will then
         * be copied to this region.
         */
        RMASampleIngressProtocol(Region region, DataLayout::Continuous const& dataLayout, std::uint32_t maxSyncBatchSize);

        /** \copydoc IngressProtocol::registerMemory()
         * \note This actually registers the memory of the internal bounce buffer, not the region passed in the constructor.
         */
        [[nodiscard]]
        virtual std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc IngressProtocol::bounceBufferInfo()
         */
        [[nodiscard]]
        virtual std::optional<TargetInfoBounceBufferInfo> bounceBufferInfo() const override;

        virtual void start(Endpoint const& endpoint) override;

        /** \copydoc IngressProtocol::processCompletion()
         */
        [[nodiscard]]
        virtual std::optional<Target::ReadResult> read(Endpoint const& endpoint, Completion const& completion) override;

        /** \brief This protocol cannot read grains, since it is designed for remote writes of audio samples.
         */
        [[nodiscard]]
        virtual bool canReadGrains() const noexcept override;

        /** \brief This protocol can read samples, since it is designed for remote writes of audio samples.
         */
        [[nodiscard]]
        virtual bool canReadSamples() const noexcept override;

        /** \copydoc IngressProtocol::destroy()
         */
        virtual void reset() override;

    private:
        void postReceives(Endpoint const& endpoint, std::size_t count);
        bool tryPostOne(Endpoint const& endpoint);

        /** \brief Helper function to create an AudioBounceBuffer based on the given audio data layout and maximum synchronous batch size.
         * Entries are as big as the maximum number of samples that can be transferred in a single batch, which is determined by maxSyncBatchSize.
         * The number of entries is determined by how many batches are needed to cover the entire history buffer.
         */
        AudioBounceBuffer makeAudioBounceBuffer(DataLayout::Continuous const& layout, std::uint32_t maxSyncBatchSize);

    private:
        AudioBounceBuffer _bounceBuffer;
        Region _region;
        bool _isMemoryRegistered = false;
        std::optional<ImmediateDataPool> _immData{};
    };
}
