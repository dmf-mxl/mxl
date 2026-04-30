// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "AudioBounceBuffer.hpp"
#include <cstddef>
#include <cstdint>

namespace mxl::lib::fabrics::ofi
{

    // TODO: could be shared with PosixContinuousFlowReader
    void AudioBounceBuffer::getMutableMultiBufferSlices(std::uint64_t index, std::size_t count, size_t bufferLength, std::size_t sampleWordSize,
        std::size_t channelCount, std::uint8_t* baseBufferPtr, mxlMutableWrappedMultiBufferSlice& slice) noexcept
    {
        auto const startOffset = (index + bufferLength - count) % bufferLength;
        auto const endOffset = (index % bufferLength);

        auto const firstLength = (startOffset < endOffset) ? count : bufferLength - startOffset;
        auto const secondLength = count - firstLength;

        slice.base.fragments[0].pointer = baseBufferPtr + (sampleWordSize * startOffset);
        slice.base.fragments[0].size = sampleWordSize * firstLength;

        slice.base.fragments[1].pointer = baseBufferPtr;
        slice.base.fragments[1].size = sampleWordSize * secondLength;

        slice.stride = sampleWordSize * bufferLength;
        slice.count = channelCount;
    }

    AudioBounceBufferEntry::AudioBounceBufferEntry(std::uint32_t size)
        : _data(sizeof(AudioEntryHeader) + size)
    {}

    AudioEntryHeader const* AudioBounceBufferEntry::header() const noexcept
    {
        // The header is located at the beginning of the buffer, so we can reinterpret the start of the data as an AudioEntryHeader
        return std::bit_cast<AudioEntryHeader const*>(_data.data());
    }

    std::uint8_t const* AudioBounceBufferEntry::samples() const noexcept
    {
        return _data.data() + sizeof(AudioEntryHeader); // Offset the pointer by the size of the header to get to the samples
    }

    std::uint8_t const* AudioBounceBufferEntry::data() const noexcept
    {
        return _data.data();
    }

    std::size_t AudioBounceBufferEntry::size() const noexcept
    {
        return _data.size() + sizeof(AudioEntryHeader);
    }

    AudioBounceBuffer::AudioBounceBuffer(size_t entryCount, std::uint32_t entrySize, DataLayout::AudioDataLayout layout)
        : _entries(entryCount, AudioBounceBufferEntry(entrySize))
        , _layout(layout)
    {}

    std::vector<Region> AudioBounceBuffer::getRegions() const noexcept
    {
        std::vector<Region> out;
        out.reserve(_entries.size());
        for (auto const& entry : _entries)
        {
            out.emplace_back(reinterpret_cast<std::uintptr_t>(entry.data()), entry.size(), nullptr, nullptr, Region::Location::host());
        }
        return out;
    }

    size_t AudioBounceBuffer::nbEntries() const noexcept
    {
        return _entries.size();
    }

    AudioEntryHeader const& AudioBounceBuffer::unpack(std::size_t entryIndex, Region& outRegion) const noexcept
    {
        auto const& entry = _entries.at(entryIndex);
        auto header = entry.header();

        // Using the given audio data layout, head index and number of samples recover the slices
        mxlMutableWrappedMultiBufferSlice slices;
        getMutableMultiBufferSlices(header->headIndex,
            header->count,
            _layout.bufferLength,
            _layout.sampleSize,
            _layout.channelCount,
            reinterpret_cast<std::uint8_t*>(outRegion.base), // NOLINT
            slices);

        auto srcAddr = entry.samples();
        for (auto& fragment : slices.base.fragments)
        {
            // check if the fragment present
            if (fragment.size > 0)
            {
                for (size_t chan = 0; chan < slices.count; chan++)
                {
                    auto dstAddr = reinterpret_cast<std::uintptr_t>(fragment.pointer) + (slices.stride * chan);
                    std::memcpy(reinterpret_cast<void*>(dstAddr), srcAddr, fragment.size); // NOLINT
                    srcAddr += fragment.size;
                }
            }
        }

        return *header;
    }
}
