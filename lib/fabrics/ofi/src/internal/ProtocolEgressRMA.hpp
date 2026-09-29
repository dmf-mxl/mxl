// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <rdma/fabric.h>
#include "AudioBounceBuffer.hpp"
#include "DataLayout.hpp"
#include "Endpoint.hpp"
#include "Protocol.hpp"

namespace mxl::lib::fabrics::ofi
{
    class RMAGrainEgressProtocol /*final*/ : public EgressProtocol
    {
    public:
        /** \copydoc EgressProtocol::registerMemory()
         * \note This is a NOP for this protocol
         */
        virtual void registerMemory(std::shared_ptr<Domain> domain) final;

        /** \copydoc EgressProtocol::transferGrain()
         */
        virtual void transferGrain(Endpoint const& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) final;

        /** \copydoc EgressProtocol::transferSamples()
         * \note This is unsupported in this protocol since it is designed for grain data. Calling this function will result in an
         * Exception::invalidState being thrown.
         */
        virtual void transferSamples(Endpoint const&, std::uint64_t, std::size_t, ::fi_addr_t) final;

        /** \copydoc EgressProtocol::processCompletion()
         */
        virtual void processCompletion(Completion::Data const&) final;

        /** \copydoc EgressProtocol::processCompletionError()
         */
        virtual void processCompletionError(Completion::Error const&) final;

        /** \copydoc EgressProtocol::hasPendingWork()
         */
        [[nodiscard]]
        virtual bool hasPendingWork() const final;

        /** \copydoc EgressProtocol::reset()
         */
        virtual std::size_t reset() final;

    private:
        friend class RMAGrainEgressProtocolTemplate;

        RMAGrainEgressProtocol(Completion::Token token, TargetInfo info, DataLayout::Discrete dataLayout, std::vector<LocalRegion> _localRegions);

    private:
        Completion::Token _token;
        TargetInfo _remoteInfo;
        DataLayout::Discrete _layout;
        std::vector<LocalRegion> _localRegions;
        std::size_t _pending = 0;
    };

    /** \brief Template for creating an Egress protocol for RMA writer endpoint to handle transferring grains to remote targets using remote write
     * operations.
     */
    class RMAGrainEgressProtocolTemplate final : public EgressProtocolTemplate
    {
    public:
        RMAGrainEgressProtocolTemplate(DataLayout::Discrete layout, std::vector<Region> regions);

        virtual void registerMemory(std::shared_ptr<Domain> domain) override;
        virtual std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout::Discrete _layout;
        std::vector<Region> _regions;
        std::optional<std::vector<LocalRegion>> _localRegions{};
    };

    //
    // RMASampleEgressProtocol below
    class RMASampleEgressProtocol /*final*/ : public EgressProtocol
    {
    public:
        /** \copydoc EgressProtocol::registerMemory()
         * \note With this protocol, the staging copy of the bounce buffer entries will get registered.
         */
        virtual void registerMemory(std::shared_ptr<Domain> domain) final;

        /** \copydoc EgressProtocol::transferGrain()
         *\note This is unsupported in this protocol since it is designed for audio data. Calling this function will result in an
         * Exception::invalidState being thrown.
         */
        virtual void transferGrain(Endpoint const& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) final;

        /** \copydoc EgressProtocol::transferSamples()
         */
        virtual void transferSamples(Endpoint const& ep, std::uint64_t headIndex, std::size_t count, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) final;

        /** \copydoc EgressProtocol::processCompletion()
         */
        virtual void processCompletion(Completion::Data const&) final;

        /** \copydoc EgressProtocol::processCompletionError()
         */
        virtual void processCompletionError(Completion::Error const&) final;

        /** \copydoc EgressProtocol::hasPendingWork()
         */
        [[nodiscard]]
        virtual bool hasPendingWork() const final;

        /** \copydoc EgressProtocol::reset()
         */
        virtual std::size_t reset() final;

    private:
        friend class RMASampleEgressProtocolTemplate;

    private:
        RMASampleEgressProtocol(Completion::Token token, TargetInfo info, DataLayout::Continuous dataLayout, LocalRegion _localRegion,
            std::size_t bounceBufferEntryCount);

        /** \brief Copy the samples of a transfer into a staging entry, channel after channel, the order the target unpacks them in.
         * \param layout The audio data layout.
         * \param headIndex The head index of the audio samples to transfer.
         * \param count The number of samples per channel to transfer.
         * \param region The local region corresponding to the user provided audio region.
         * \param dst Where the samples of the first channel go.
         */
        static void copySamples(DataLayout::Continuous const& layout, std::uint64_t headIndex, std::size_t count, LocalRegion const& region,
            std::uint8_t* dst);

    private:
        Completion::Token _token;
        TargetInfo _remoteInfo;
        DataLayout::Continuous _layout;
        LocalRegion _localRegion;                     /**< Registered local region corresponding to the user provided audio region.  */
        std::vector<std::uint8_t> _staging; /**< One registered copy of each remote bounce buffer entry, header and samples, so a transfer is a
                                               single write. */
        std::optional<LocalRegion> _stagingRegion; /**< The registered region backing _staging. */
        std::size_t _pending = 0;
        std::uint32_t _bounceBufferEntryIndex{0};     /**< The index of the bounce buffer entry to use for the next transfer. */
        std::size_t _bounceBufferEntryCount; /**< The total number of bounce buffer entries. Used to wrap around the bounce buffer entry index. */
    };

    /** \brief Template for creating an Egress protocol for RMA writer endpoint to handles transferring audio data to remote targets using remote
     * write operations.
     */
    class RMASampleEgressProtocolTemplate final : public EgressProtocolTemplate
    {
    public:
        RMASampleEgressProtocolTemplate(DataLayout::Continuous layout, Region region);

        virtual void registerMemory(std::shared_ptr<Domain> domain) override;
        virtual std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout::Continuous _layout;
        Region _region;                          /**< Region provided by the user. */
        std::optional<LocalRegion> _localRegion; /**< Registered local region corresponding to the user provided region. This is what will actually be
                                                    used for remote writes. */
    };

}
