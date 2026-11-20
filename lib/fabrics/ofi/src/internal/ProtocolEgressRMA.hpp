// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
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

        /** \copydoc EgressProtocol::hasPendingWork()
         */
        [[nodiscard]]
        virtual bool hasPendingWork() const final;

        /** \copydoc EgressProtocol::reset()
         */
        virtual std::size_t reset() final;

    private:
        friend class RMAGrainEgressProtocolTemplate;

        RMAGrainEgressProtocol(Completion::Token token, TargetInfo info, DataLayout::Discrete dataLayout, std::vector<LocalRegion> localRegions,
            std::vector<Region> sourceRegions, std::vector<Region> bounceRegions, std::vector<std::shared_ptr<void>> bounceOwners,
            std::size_t regionsPerGrain);

        /** \brief Write the planes of one grain slice from a split grain (header and payload in separate regions). */
        void transferSplitGrain(Endpoint const& ep, std::size_t localSlot, std::size_t remoteSlot, SliceRange const& sliceRange,
            ::fi_addr_t destAddr);

        /** \brief Copy a byte range of a device payload into its bounce buffer, so that the registered memory holds the data to send. */
        void refreshBounceBuffer(std::size_t localSlot, std::uint64_t offset, std::size_t size);

    private:
        Completion::Token _token;
        TargetInfo _remoteInfo;
        DataLayout::Discrete _layout;
        std::vector<LocalRegion> _localRegions;
        /** Regions as the caller gave them. For device payloads they are the source of the bounce copies. */
        std::vector<Region> _sourceRegions;
        /** Registered regions. For device payloads they are the bounce buffers. */
        std::vector<Region> _bounceRegions;
        /** Keeps the bounce buffers alive for as long as this protocol instance exists. */
        std::vector<std::shared_ptr<void>> _bounceOwners;
        std::size_t _regionsPerGrain{1};
        std::size_t _pending = 0;
    };

    /** \brief Template for creating an Egress protocol for RMA writer endpoint to handle transferring grains to remote targets using remote write
     * operations.
     */
    class RMAGrainEgressProtocolTemplate final : public EgressProtocolTemplate
    {
    public:
        /**
         * \param regionsPerGrain 1 when a grain is one contiguous region, 2 when it is a header region followed by a payload region.
         */
        RMAGrainEgressProtocolTemplate(DataLayout::Discrete layout, std::vector<Region> regions, std::size_t regionsPerGrain = 1);

        virtual void registerMemory(std::shared_ptr<Domain> domain) override;
        virtual std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        /** \brief For device payloads, allocate bounce buffers in host-registerable memory of the payload's device.
         *
         * CUDA IPC imports (a reader's view of a writer's buffer) cannot be registered with nvidia_peermem. Egress
         * registers its own device buffers and copies each slice into them before the RDMA write.
         */
        void prepareRegisterableCudaRegions();

    private:
        DataLayout::Discrete _layout;
        std::vector<Region> _sourceRegions;
        std::vector<Region> _regions;
        std::vector<std::shared_ptr<void>> _bounceOwners;
        std::size_t _regionsPerGrain{1};
        std::optional<std::vector<LocalRegion>> _localRegions{};
    };

    //
    // RMASampleEgressProtocol below
    class RMASampleEgressProtocol /*final*/ : public EgressProtocol
    {
    public:
        /** \copydoc EgressProtocol::registerMemory()
         * \note With this protocol, the audio entry headers will get registered.
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

        virtual void registerMemory(std::shared_ptr<Domain> domain) override;
        virtual std::unique_ptr<EgressProtocol> createInstance(Completion::Token, TargetInfo remoteInfo) override;

    private:
        DataLayout::Continuous _layout;
        Region _region;                          /**< Region provided by the user. */
        std::optional<LocalRegion> _localRegion; /**< Registered local region corresponding to the user provided region. This is what will actually be
                                                    used for remote writes. */
    };

}
