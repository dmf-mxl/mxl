// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "AudioBounceBuffer.hpp"

using namespace mxl::lib::fabrics::ofi;

namespace
{
    using Sample = std::uint32_t;

    // 2 audio channels, each target channel has a ring buffer of 9 samples and we are unpacking 5 samples landing at head index 11.
    // The start offset is (11 + 9 - 5) % 9 = 6 so each channel writes to target indices:
    //   6, 7, 8, 0, 1
    // which creates a split of 3 samples before wrap + 2 samples after wrap
    constexpr auto ChannelCount = std::size_t{2};
    constexpr auto TargetBufferLength = std::size_t{9};
    constexpr auto HeadIndex = std::uint64_t{11};
    constexpr auto SampleCount = std::size_t{5};
    constexpr auto UntouchedSample = Sample{0xFFFFFFFFU};

    std::uintptr_t regionBaseOf(void* ptr) noexcept
    {
        return reinterpret_cast<std::uintptr_t>(ptr);
    }

    std::size_t ringStartOffset(std::uint64_t headIndex, std::size_t count, std::size_t bufferLength) noexcept
    {
        return (headIndex + bufferLength - count) % bufferLength;
    }

    Sample& sampleAt(std::vector<Sample>& samples, std::size_t channel, std::size_t index, std::size_t bufferLength)
    {
        return samples[(channel * bufferLength) + index];
    }
}

TEST_CASE("ofi: audio bounce buffer unpacks channel-major payload across ring wrap", "[ofi][AudioBounceBuffer]")
{
    auto target = std::vector<Sample>(ChannelCount * TargetBufferLength, UntouchedSample);
    auto targetSlice = mxlMutableWrappedMultiBufferSlice{};
    AudioBounceBuffer::getMutableMultiBufferSlices(
        HeadIndex, SampleCount, TargetBufferLength, sizeof(Sample), ChannelCount, reinterpret_cast<std::uint8_t*>(target.data()), targetSlice);

    REQUIRE((targetSlice.base.fragments[0].size / sizeof(Sample)) == 3U);
    REQUIRE((targetSlice.base.fragments[1].size / sizeof(Sample)) == 2U);

    // channel 0 payload: 103, 104, 105, 106, 107
    // channel 1 payload: 203, 204, 205, 206, 207
    auto const payloadSamples = std::vector<Sample>{103U, 104U, 105U, 106U, 107U, 203U, 204U, 205U, 206U, 207U};
    auto const payloadSize = payloadSamples.size() * sizeof(Sample);

    auto const targetLayout = DataLayout::Continuous{.sampleSize = sizeof(Sample), .channelCount = ChannelCount, .bufferLength = TargetBufferLength};
    auto bounceBuffer = AudioBounceBuffer{1, payloadSize, targetLayout};
    auto bounceRegions = bounceBuffer.getRegions();
    REQUIRE(bounceRegions.size() == 1U);
    REQUIRE(bounceRegions.front().size == (sizeof(AudioEntryHeader) + payloadSize));

    // Write directly into the bounce-buffer entry:
    auto* entryData = reinterpret_cast<std::uint8_t*>(bounceRegions.front().base); // NOLINT
    auto const header = AudioEntryHeader{.headIndex = HeadIndex, .count = SampleCount};
    std::memcpy(entryData, &header, sizeof(header));
    std::memcpy(entryData + sizeof(AudioEntryHeader), payloadSamples.data(), payloadSize);

    // Let entry 0 of the bounce buffer copy its payload into the target ring buffer.
    auto const outRegion = Region{regionBaseOf(target.data()), target.size() * sizeof(Sample), nullptr, nullptr, Region::Location::host()};
    auto const unpackedHeader = bounceBuffer.unpack(0, outRegion);
    REQUIRE(unpackedHeader.headIndex == HeadIndex);
    REQUIRE(unpackedHeader.count == SampleCount);

    // Verify that unpack did correctly consume channel-major payload
    auto expectedTarget = std::vector<Sample>(ChannelCount * TargetBufferLength, UntouchedSample);
    auto const targetStartOffset = ringStartOffset(HeadIndex, SampleCount, TargetBufferLength);
    for (auto channel = std::size_t{0}; channel < ChannelCount; channel++)
    {
        for (auto offset = std::size_t{0}; offset < SampleCount; offset++)
        {
            auto const targetIndex = (targetStartOffset + offset) % TargetBufferLength;
            sampleAt(expectedTarget, channel, targetIndex, TargetBufferLength) = payloadSamples[(channel * SampleCount) + offset];
        }
    }

    REQUIRE(target == expectedTarget);
}
