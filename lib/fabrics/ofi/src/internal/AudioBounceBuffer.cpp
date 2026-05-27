// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "AudioBounceBuffer.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <bit>
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    // TODO: could be shared with PosixContinuousFlowReader
    void AudioBounceBuffer::getMutableMultiBufferSlices(std::uint64_t index, std::size_t count, std::size_t bufferLength, std::size_t sampleWordSize,
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
        return _data.size();
    }

    AudioBounceBuffer::AudioBounceBuffer(std::size_t entryCount, std::size_t entrySize, DataLayout::Continuous layout)
        : _entries{entryCount, AudioBounceBufferEntry{static_cast<std::uint32_t>(entrySize)}}
        , _layout{layout}
    {}

    std::vector<Region> AudioBounceBuffer::getRegions() const noexcept
    {
        auto out = std::vector<Region>{};
        out.reserve(_entries.size());
        for (auto const& entry : _entries)
        {
            out.emplace_back(reinterpret_cast<std::uintptr_t>(entry.data()), entry.size(), nullptr, nullptr, Region::Location::host());
        }
        return out;
    }

    std::size_t AudioBounceBuffer::entryCount() const noexcept
    {
        return _entries.size();
    }

    std::size_t AudioBounceBuffer::entrySize() const noexcept
    {
        return _entries.empty() ? 0 : _entries.front().size();
    }

    AudioEntryHeader const& AudioBounceBuffer::unpack(std::size_t entryIndex, Region const& outRegion) const
    {
        auto const& entry = _entries.at(entryIndex);
        auto const* header = entry.header();

        auto const maxCountPerEntry = (entrySize() - sizeof(AudioEntryHeader)) / (_layout.channelCount * _layout.sampleSize);

        if (header->count > maxCountPerEntry)
        {
            throw Exception::invalidArgument(
                "Invalid 'count' {} received in the header. That number of samples per channel would bust the bounce buffer entry.", header->count);
        }

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
                for (auto chan = std::size_t{0}; chan < slices.count; chan++)
                {
                    auto const dstAddr = reinterpret_cast<std::uint8_t*>(fragment.pointer) + (slices.stride * chan);
                    std::memcpy(dstAddr, srcAddr, fragment.size);
                    srcAddr += fragment.size;
                }
            }
        }

        return *header;
    }
}
