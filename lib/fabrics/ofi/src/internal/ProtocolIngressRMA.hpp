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
         * \param region The memory region containing audio. The audio samples will be first received in one of the bounce buffer entry and will
   then be copied to this region.
         */
        RMASampleIngressProtocol(Region region, DataLayout::AudioDataLayout const& dataLayout, std::uint32_t maxSyncBatchSize);

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

        /** \copydoc IngressProtocol::destroy()
         */
        void reset() override;

    private:
        LocalRegion immDataRegion();

    private:
        AudioBounceBuffer _bounceBuffer;
        Region _region;
        bool _isMemoryRegistered{false};
        std::optional<Target::ImmediateDataLocation> _immDataBuffer{};
    };
}
