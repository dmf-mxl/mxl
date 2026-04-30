// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Header for an audio bounce buffer entry.
     */
    struct AudioEntryHeader
    {
        std::uint64_t headIndex;
        std::size_t count;
    };

    /**\brief An audio bounce buffer entry is backed by a collection of samples
     * The layout is:  AudioEntryHeader, followed by samples of ch0, followed by samples of ch1, etc.
     */
    class AudioBounceBufferEntry

    {
    public:
        /** Construct an AudioBounceBufferEntry with the given size in bytes. The size should be large enough to hold the samples for all channels.
         *\note The header size is not included in the size parameter, it will be added on top of it. The total size of the bounce buffer entry will
         * be header size + size parameter.
         */
        AudioBounceBufferEntry(std::uint32_t size);

        /** Return the header of the bounce buffer entry. The header is located at the beginning of the entry data. */
        [[nodiscard]]
        AudioEntryHeader const* header() const noexcept;

        /** Return a pointer to the samples data of the bounce buffer entry. The samples data is located immediately after the header.
         */
        [[nodiscard]]
        std::uint8_t const* samples() const noexcept;

        /** Return a pointer to the beginning of the bounce buffer entry data, including the header.
         */
        [[nodiscard]]
        std::uint8_t const* data() const noexcept;

        /** The total size of the bounce buffer entry in bytes, including header and samples. */
        [[nodiscard]]
        std::size_t size() const noexcept;

    private:
        std::vector<std::uint8_t> _data;
    };

    /** \brief A bounce buffer for audio samples.
     *
     * For audio samples, a bounce buffer is used to receive the samples from the network before copying them to the final destination region. This is
     * necessary because the samples to transfer on the Initiator are scattered at multiple locations (one per channel), but only a single memory
     * address can be provided for the destination.
     */
    class AudioBounceBuffer
    {
    public:
        AudioBounceBuffer(size_t entryCount, std::uint32_t entrySize, DataLayout::AudioDataLayout layout);

        /** Return the regions corresponding to the bounce buffer entries. Each region corresponds to one bounce buffer entry. The regions are
         * returned in the same order as the bounce buffer entries. */
        [[nodiscard]]
        std::vector<Region> getRegions() const noexcept;

        /** Return the number of entries in the bounce buffer. */
        [[nodiscard]]
        size_t nbEntries() const noexcept;

        /** Copy the samples from the bounce buffer entry to the provided output region, and return the header of the entry. Internally the header
         * will be used to correctly map the bounce buffer entry slices to the output region slices.
         */
        AudioEntryHeader const& unpack(std::size_t entryIndex, Region& outRegion) const noexcept;

        static void getMutableMultiBufferSlices(std::uint64_t index, std::size_t count, size_t bufferLength, std::size_t sampleWordSize,
            std::size_t channelCount, std::uint8_t* baseBufferPtr, mxlMutableWrappedMultiBufferSlice& slice) noexcept;

    private:
        std::vector<AudioBounceBufferEntry> _entries;
        DataLayout::AudioDataLayout _layout;
    };
}
