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
    class RMAGrainEgressProtocol : public EgressProtocol
    {
    public:
        /** \copydoc EgressProtocol::registerMemory()
         * \note This is a NOP for this protocol
         */
        void registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc EgressProtocol::transferGrain()
         */
        void transferGrain(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) override;

        /** \copydoc EgressProtocol::transferSamples()
         * \note This is unsupported in this protocol since it is designed for video data. Calling this function will result in an
         * Exception::invalidState being thrown.
         */
        void transferSamples(Endpoint&, std::uint64_t, std::size_t, ::fi_addr_t) override;

        /** \copydoc EgressProtocol::processCompletion()
         */
        void processCompletion(Completion::Data const&) override;

        /** \copydoc EgressProtocol::hasPendingWork()
         */
        [[nodiscard]]
        bool hasPendingWork() const override;

        /** \copydoc EgressProtocol::reset()
         */
        std::size_t reset() override;

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

    /** \brief Template for creating an Egress protocol for RMA writer endpoint to Handle transferring grains to remote targets using remote write
     * operations.
     */
    class RMAGrainEgressProtocolTemplate final : public EgressProtocolTemplate
    {
    public:
        RMAGrainEgressProtocolTemplate(DataLayout::Discrete layout, std::vector<Region> regions);

        void registerMemory(std::shared_ptr<Domain> domain) override;
        std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout::Discrete _layout;
        std::vector<Region> _regions;
        std::optional<std::vector<LocalRegion>> _localRegions{};
    };

    //
    // RMASampleEgressProtocol below
    class RMASampleEgressProtocol : public EgressProtocol
    {
    public:
        /** \copydoc EgressProtocol::registerMemory()
         * \note With this protocol, the audio entry headers will get registered.
         */
        void registerMemory(std::shared_ptr<Domain> domain) override;

        /** \copydoc EgressProtocol::transferGrain()
         *\note This is unsupported in this protocol since it is designed for audio data. Calling this function will result in an
         * Exception::invalidState being thrown.
         */
        void transferGrain(Endpoint& ep, std::uint64_t localIndex, std::uint64_t remoteIndex, std::uint32_t payloadOffset,
            SliceRange const& sliceRange, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) override;

        /** \copydoc EgressProtocol::transferSamples()
         */
        void transferSamples(Endpoint& ep, std::uint64_t headIndex, std::size_t count, ::fi_addr_t destAddr = FI_ADDR_UNSPEC) override;

        /** \copydoc EgressProtocol::processCompletion()
         */
        void processCompletion(Completion::Data const&) override;

        /** \copydoc EgressProtocol::hasPendingWork()
         */
        [[nodiscard]]
        bool hasPendingWork() const override;

        /** \copydoc EgressProtocol::reset()
         */
        std::size_t reset() override;

    private:
        friend class RMASampleEgressProtocolTemplate;

    private:
        RMASampleEgressProtocol(Completion::Token token, TargetInfo info, DataLayout::Continuous dataLayout, LocalRegion _localRegion,
            std::size_t bounceBufferEntryCount);

        /** \brief Create the scatter-gather list for a given audio region and data layout. This will be used for the remote write transfer. The list
         * will be created based on the head index and count of samples to transfer.
         * \param layout The audio data layout.
         * \param headIndex The head index of the audio samples to transfer.
         * \param count The number of samples per channel to transfer.
         * \param region The local region corresponding to the user provided audio region. This is used to calculate the addresses for the
         * scatter-gather list entries.
         * \return A vector of local regions representing the scatter-gather list for the transfer.
         */
        static std::vector<LocalRegion> makeScatterGatherList(DataLayout::Continuous const& layout, std::uint64_t headIndex, std::size_t count,
            LocalRegion const& region);

    private:
        Completion::Token _token;
        TargetInfo _remoteInfo;
        DataLayout::Continuous _layout;
        LocalRegion _localRegion;                     /**< Registered local region corresponding to the user provided audio region.  */
        std::vector<AudioEntryHeader> _entryHeaders;  /**< A vector of audio entry headers used for the bounce buffer. This is needed to keep track of
                                                        the metadata of each bounce buffer entry. */
        std::vector<LocalRegion> _entryHeaderRegions; /**< A vector of local regions corresponding to the entry headers. These regions are registered
                                                         and used for remote writes to the bounce buffer. */
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

        void registerMemory(std::shared_ptr<Domain> domain) override;
        std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout::Continuous _layout;
        Region _region;                          /**< Region provided by the user. */
        std::optional<LocalRegion> _localRegion; /**< Registered local region corresponding to the user provided region. This is what will actually be
                                                    used for remote writes. */
    };

}
