// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AudioBounceBuffer.hpp"
#include "DataLayout.hpp"
#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{
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
        std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc IngressProtocol::bounceBufferInfo()
         *\note This protocol does not use a bounce buffer, so this function returns an empty optional.
         */
        [[nodiscard]]
        std::optional<TargetInfoBounceBufferInfo> bounceBufferInfo() override;

        /** \copydoc IngressProtocol::start()
         */
        void start(Endpoint& endpoint) override;

        /** \copydoc IngressProtocol::processCompletion()
         */
        std::optional<Target::ReadResult> read(Endpoint& endpoint, Completion const& completion) override;

        /**\brief This protocol can read grains, but not samples, since it is designed for remote writes of grain buffers.
         */
        [[nodiscard]]
        bool canReadGrains() const noexcept override;

        /** \brief This protocol cannot read samples, since it is designed for remote writes of grain buffers.
         */
        [[nodiscard]]
        bool canReadSamples() const noexcept override;

        /** \copydoc IngressProtocol::destroy()
         */
        void reset() override;

    private:
        LocalRegion immDataRegion();

    private:
        std::vector<Region> _regions;
        bool _isMemoryRegistered{false};
        std::optional<Target::ImmediateDataLocation> _immDataBuffer{};
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
        std::vector<RemoteRegion> registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc IngressProtocol::bounceBufferInfo()
         */
        [[nodiscard]]
        std::optional<TargetInfoBounceBufferInfo> bounceBufferInfo() override;

        void start(Endpoint& endpoint) override;

        /** \copydoc IngressProtocol::processCompletion()
         */
        std::optional<Target::ReadResult> read(Endpoint& endpoint, Completion const& completion) override;

        /** \brief This protocol cannot read grains, since it is designed for remote writes of audio samples.
         */
        [[nodiscard]]
        bool canReadGrains() const noexcept override;

        /** \brief This protocol can read samples, since it is designed for remote writes of audio samples.
         */
        [[nodiscard]]
        bool canReadSamples() const noexcept override;

        /** \copydoc IngressProtocol::destroy()
         */
        void reset() override;

    private:
        LocalRegion immDataRegion();

        /** \brief Helper function to create an AudioBounceBuffer based on the given audio data layout and maximum synchronous batch size.
         * Entries are as big as the maximum number of samples that can be transferred in a single batch, which is determined by maxSyncBatchSize.
         * The number of entries is determined by how many batches are needed to cover the entire history buffer.
         */
        AudioBounceBuffer makeAudioBounceBuffer(DataLayout::Continuous const& layout, std::uint32_t maxSyncBatchSize);

    private:
        AudioBounceBuffer _bounceBuffer;
        Region _region;
        bool _isMemoryRegistered = false;
        std::optional<Target::ImmediateDataLocation> _immDataBuffer{};
    };
}
