// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Event API, fragmentation, lifetime and concurrency regression tests.
 */

#include <csignal>
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <fstream>
#include <future>
#include <limits>
#include <span>
#include <thread>
#include <vector>
#include <poll.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fmt/format.h>
#include <Packet.h>
#include <PcapFileDevice.h>
#include <RawPacket.h>
#include <UdpLayer.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/EventRingBuffer.hpp"
#include "mxl-internal/Flow.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/PosixFlowIoFactory.hpp"
#include "mxl-internal/Sync.hpp"
#include "Utils.hpp"

namespace
{
    /// Stable identifier reused only within isolated temporary test domains.
    constexpr auto id = "cabbc00d-3860-4438-bc48-8ebdfe67305e";
    /// Event descriptor whose 20 Hz rate and default 200 ms history produce four slots.
    constexpr auto definition = R"({"id":"cabbc00d-3860-4438-bc48-8ebdfe67305e",
        "format":"urn:x-nmos:format:event","label":"Events",
        "grain_rate":{"numerator":20},
        "tags":{"urn:x-nmos:tag:grouphint/v1.0":["Test:Events"]}})";

    /**
     * @brief Build a fixture descriptor with a replacement rational grain rate.
     * @param grainRate JSON object containing numerator and optional denominator.
     * @return Serialized descriptor using the requested rate.
     */
    std::string definitionWithRate(char const* grainRate)
    {
        // Replace only the rate so capacity tests share the same otherwise-valid descriptor.
        auto flow = picojson::value{};
        auto rate = picojson::value{};
        REQUIRE(picojson::parse(flow, definition).empty());
        REQUIRE(picojson::parse(rate, grainRate).empty());
        flow.get<picojson::object>()["grain_rate"] = rate;
        return flow.serialize();
    }

    /** @brief Own an independent reader; one instance is required per cursor. */
    struct IndependentEventReader
    {
        mxlInstance instance{};
        mxlFlowReader reader{};

        explicit IndependentEventReader(std::filesystem::path const& domain)
            : instance{mxlCreateInstance(domain.c_str(), "{}")}
        {
            REQUIRE(instance);
            REQUIRE(mxlCreateFlowReader(instance, id, "", &reader) == MXL_STATUS_OK);
        }

        ~IndependentEventReader()
        {
            mxlReleaseFlowReader(instance, reader);
            mxlDestroyInstance(instance);
        }
    };

    /** @brief Isolated event flow with separate producer and consumer instances and automatic cleanup. */
    struct EventFixture
    {
        std::filesystem::path domain = mxl::tests::makeTempDomain(); ///< Owned temporary domain directory.
        mxlInstance producer{};                                      ///< Instance owning the single writer.
        mxlInstance consumer{};                                      ///< Independent reader instance.
        mxlFlowWriter writer{};                                      ///< Producer handle, null when a test has released it.
        mxlFlowReader reader{};                                      ///< Consumer handle, null when a test has released it.
        mxlFlowConfigInfo config{};                                  ///< Geometry returned when the fixture flow is created.

        /**
         * @brief Create the domain, producer, event flow and independent consumer.
         * @param flowDef Event descriptor JSON to use for creation.
         * @param domainOptions Optional domain options JSON written before instance creation.
         */
        EventFixture(std::string const& flowDef = definition, char const* domainOptions = nullptr)
        {
            // Write domain options before creating either instance: capacity is fixed at flow creation.
            if (domainOptions)
            {
                auto options = std::ofstream{mxl::lib::makeDomainOptionsFilePath(domain)};
                REQUIRE(options.is_open());
                options << domainOptions;
            }
            // Use separate instances so the reader opens an independent mapping of the writer's flow.
            producer = mxlCreateInstance(domain.c_str(), "{}");
            consumer = mxlCreateInstance(domain.c_str(), "{}");
            REQUIRE(producer);
            REQUIRE(consumer);
            REQUIRE(mxlCreateFlowWriter(producer, flowDef.c_str(), nullptr, &writer, &config, nullptr) == MXL_STATUS_OK);
            REQUIRE(mxlCreateFlowReader(consumer, id, "", &reader) == MXL_STATUS_OK);
        }

        /// Release handles and instances, then remove the temporary domain.
        ~EventFixture()
        {
            // Release mappings before removing the temporary shared-memory files.
            mxlReleaseFlowReader(consumer, reader);
            mxlReleaseFlowWriter(producer, writer);
            mxlDestroyInstance(consumer);
            mxlDestroyInstance(producer);
            std::filesystem::remove_all(domain);
        }

        /**
         * @brief Commit an unfragmented event whose payload is its timestamp.
         * @param timestamp Nondecreasing timestamp value to publish and encode in the payload.
         */
        void write(uint64_t timestamp)
        {
            // Encode the timestamp in the payload so reads can detect mismatched metadata and data.
            auto info = mxlEventInfo{};
            auto payload = static_cast<uint8_t*>(nullptr);
            REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
            REQUIRE(info.eventSize == 0);
            info.timestamp = timestamp;
            info.eventSize = sizeof timestamp;
            info.offset = 0;
            info.complete = 1;
            std::memcpy(payload, &timestamp, sizeof timestamp);
            REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
        }

        /**
         * @brief Read and validate an unfragmented timestamp-pattern entry.
         * @param timestamp Expected metadata timestamp and payload value.
         */
        void read(uint64_t timestamp)
        {
            // Verify both the queue metadata and the timestamp copied through shared memory.
            auto info = mxlEventInfo{};
            auto payload = static_cast<uint8_t*>(nullptr);
            REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
            REQUIRE(info.timestamp == timestamp);
            REQUIRE(info.eventSize == sizeof timestamp);
            REQUIRE(info.offset == 0);
            REQUIRE(info.complete == 1);
            auto value = uint64_t{};
            std::memcpy(&value, payload, sizeof value);
            REQUIRE(value == timestamp);
        }

        /**
         * @brief Publish a fragment using a fixed SMPTE registry and DIT.
         * @param timestamp Timestamp shared by fragments of the logical event.
         * @param bytes Fragment payload to copy.
         * @param offset Logical offset, including deliberately discontinuous test values.
         * @param complete True for the final fragment or an unfragmented entry.
         */
        void writeFragment(std::uint64_t timestamp, std::span<std::uint8_t const> bytes, std::uint32_t offset, bool complete)
        {
            // Preserve the supplied offset, including malformed values used by discontinuity tests.
            auto info = mxlEventInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
            REQUIRE(bytes.size() <= config.event.eventPayloadSize);
            info.timestamp = timestamp;
            info.registryType = MXL_EVENT_REGISTRY_TYPE_SMPTE;
            std::strcpy(info.dataItemType, "010203");
            info.eventSize = static_cast<std::uint32_t>(bytes.size());
            info.offset = offset;
            info.complete = complete ? 1 : 0;
            if (!bytes.empty())
            {
                std::memcpy(payload, bytes.data(), bytes.size());
            }
            REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
        }

        /** @return Validated fragment metadata and an owned payload copy for subsequent assembly checks. */
        std::pair<mxlEventInfo, std::vector<std::uint8_t>> readFragment()
        {
            // Own a copy of the fragment bytes so subsequent reads cannot invalidate assembly checks.
            auto info = mxlEventInfo{};
            auto payload = static_cast<std::uint8_t*>(nullptr);
            REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
            REQUIRE(info.version == mxl::lib::EVENT_HEADER_VERSION);
            REQUIRE(info.size == sizeof info);
            REQUIRE(info.eventSize <= config.event.eventPayloadSize);
            REQUIRE(info.registryType == MXL_EVENT_REGISTRY_TYPE_SMPTE);
            REQUIRE(std::strcmp(info.dataItemType, "010203") == 0);
            return {
                info, std::vector<std::uint8_t>{payload, payload + info.eventSize}
            };
        }

        /**
         * @brief Assert the flow's last committed queue index.
         * @param head Expected head, including MXL_UNDEFINED_INDEX for an empty flow.
         */
        void checkHead(uint64_t head)
        {
            // The head identifies the last committed entry, not the next free position.
            auto runtime = mxlFlowRuntimeInfo{};
            REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtime) == MXL_STATUS_OK);
            REQUIRE(runtime.headIndex == head);
        }
    };

    /** @brief Expected stream geometry and contents for one ST 2110-41 test capture. */
    struct St2110Capture
    {
        char const* path{};                         ///< Pcap path relative to the test working directory.
        std::size_t eventCount{};                   ///< Number of complete application events in the capture.
        std::size_t announcementCount{};            ///< Number of SAP/SDP packets excluded from the event flow.
        std::uint8_t payloadType{};                 ///< Dynamically assigned RTP payload type for ST 2110-41.
        std::uint16_t firstSequence{};              ///< Sequence number of the first RTP packet.
        std::uint32_t firstTimestamp{};             ///< Timestamp of the first event in the configured 48 kHz clock.
        std::vector<std::uint32_t> fragmentSizes{}; ///< Ordered fragment sizes in bytes, repeated for each event.
        std::uint32_t dataItemType{0x100};          ///< Expected SMPTE data item type shared by the fragments.
    };

    /**
     * @brief Decode a network-order word without alignment or host-endian assumptions.
     * @param bytes Four bytes in network order.
     * @return Unsigned host-order value.
     */
    std::uint32_t readNetworkWord(std::span<std::uint8_t const, 4> bytes) noexcept
    {
        // Widen each byte before shifting to decode safely without an aligned integer load.
        return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) | (std::uint32_t{bytes[2]} << 8) | std::uint32_t{bytes[3]};
    }

    /**
     * @brief Convert one captured ST 2110-41 Serial ADM packet into an event queue entry.
     *
     * The supported captures have fixed RTP headers and one data item per packet using
     * Annex A segmentation. Payload type, data item length, offset and K bit come from the packet.
     * DIL includes the offset word; the SMPTE advisory corrects the segment length to DIL - 1.
     * The test configures a 48 kHz RTP clock and a synthetic TAI epoch, since the capture
     * alone does not establish an absolute RTP-to-TAI mapping. Timestamp wrap is absent.
     *
     * @see https://www.smpte.org/standards/advisory-note-2110-41
     * @param writer Open event flow writer owned by the test.
     * @param rtp Complete RTP packet bytes, excluding UDP headers.
     * @param taiEpoch Synthetic TAI nanosecond value corresponding to RTP timestamp zero.
     * @param capacity Payload capacity returned in the event flow configuration.
     */
    void writeSt2110Packet(mxlFlowWriter writer, std::span<std::uint8_t const> rtp, std::uint64_t taiEpoch, std::uint32_t capacity)
    {
        REQUIRE(rtp.size() >= 20);
        // Validate the supported RTP header layout, zero marker bit, and dynamic payload type.
        REQUIRE(rtp[0] == 0x80);
        REQUIRE(rtp[1] >= 96);
        REQUIRE(rtp[1] <= 127);
        auto const timestamp = readNetworkWord(rtp.subspan<4, 4>());
        // Decode the 22-bit DIT, completion bit and 9-bit word count, which includes the offset word.
        auto const itemHeader = readNetworkWord(rtp.subspan<12, 4>());
        auto const lengthWords = itemHeader & 0x1FFU;
        REQUIRE(lengthWords > 1);
        REQUIRE(rtp.size() == 16 + (std::size_t{lengthWords} * 4));
        auto const offsetWords = readNetworkWord(rtp.subspan<16, 4>());
        REQUIRE(offsetWords <= std::numeric_limits<std::uint32_t>::max() / 4);
        auto const data = rtp.subspan(20);

        // Transport headers become event metadata. Only the segment bytes are copied into the ring.
        auto info = mxlEventInfo{};
        auto buffer = static_cast<std::uint8_t*>(nullptr);
        REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &buffer) == MXL_STATUS_OK);
        REQUIRE(buffer != nullptr);
        REQUIRE(data.size() <= capacity);
        info.timestamp = taiEpoch + (std::uint64_t{timestamp} * 1'000'000'000 / 48'000);
        info.registryType = MXL_EVENT_REGISTRY_TYPE_SMPTE;
        auto const dit = fmt::format("{:X}", itemHeader >> 10);
        REQUIRE(dit.size() < sizeof info.dataItemType);
        std::memcpy(info.dataItemType, dit.c_str(), dit.size() + 1);
        info.eventSize = static_cast<std::uint32_t>(data.size());
        // ST 2110-41 offsets count 32-bit words; MXL offsets count bytes within the logical event.
        info.offset = offsetWords * 4;
        info.complete = static_cast<std::uint8_t>((itemHeader >> 9) & 1U);
        std::memcpy(buffer, data.data(), data.size());
        REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
    }
}

/** @test Event flow queue preserves timestamps and complete payloads. */
TEST_CASE_METHOD(EventFixture, "Event flow queue preserves timestamps and complete payloads", "[events]")
{
    REQUIRE(config.common.format == MXL_DATA_FORMAT_EVENT);
    REQUIRE(config.common.grainRate.numerator == 20);
    REQUIRE(config.common.grainRate.denominator == 1);
    REQUIRE(config.event.eventCount == 4);
    REQUIRE(config.event.eventPayloadSize == MXL_DATA_FORMAT_GRAIN_SIZE);
    auto info = mxlEventInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
    // Zero and duplicates are valid, but committed timestamps must not decrease.
    for (auto timestamp : {0ULL, 0ULL, 3ULL, 500ULL, 500ULL, 900ULL})
    {
        write(timestamp);
        read(timestamp);
    }
    REQUIRE(mxlFlowReaderGetEvent(reader, 1000000, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
}

/**
 * @test Complete and fragmented ST 2110-41 captures survive an event ring round trip.
 * Verify each fragment's metadata and payload against the pcap, enforce offset continuity,
 * and compare reassembled events, including word-alignment padding. The ring is sized for
 * 50 entries per second over the default 200 ms history duration (10 slots).
 */
TEST_CASE("ST 2110-41 pcap packets round trip through the event ring", "[events][pcap]")
{
    // Run both captures against expectations that are independent of the conversion helper.
    auto const capture = GENERATE(
        St2110Capture{
            "data/ST2110-41-DIT-100-Serial-ADM-compressed-25frames.pcap", 25, 0, 122, 37158, 15804720, {1328}
    },
        St2110Capture{"data/st2110-41-fragmented.pcap", 125, 1, 98, 4500, 894525369, {1436, 28}});
    CAPTURE(capture.path);
    auto fixture = EventFixture{definitionWithRate(R"({"numerator":50})")};
    auto pcapReader = pcpp::PcapFileReaderDevice{capture.path};
    REQUIRE(pcapReader.open());
    auto packets = pcpp::RawPacketVector{};
    // Count RTP fragments separately from logical events and exclude the SAP announcement.
    auto const expectedPackets = capture.eventCount * capture.fragmentSizes.size();
    auto const packetsRead = pcapReader.getNextPackets(packets);
    REQUIRE(packetsRead >= 0);
    REQUIRE(static_cast<std::size_t>(packetsRead) == expectedPackets + capture.announcementCount);
    REQUIRE(packets.size() == expectedPackets + capture.announcementCount);
    pcapReader.close();
    REQUIRE(fixture.config.common.grainRate.numerator == 50);
    REQUIRE(fixture.config.common.grainRate.denominator == 1);
    REQUIRE(fixture.config.event.eventCount == 10);

    /// Synthetic TAI epoch used to make RTP timestamp conversion deterministic.
    constexpr auto taiEpoch = std::uint64_t{1'000'000'000'000'000'000};
    auto packetIndex = std::size_t{};
    auto completedEvents = std::size_t{};
    auto announcements = std::size_t{};
    auto originalEvent = std::vector<std::uint8_t>{};
    auto receivedEvent = std::vector<std::uint8_t>{};
    for (auto rawPacket : packets)
    {
        CAPTURE(packetIndex);
        auto packet = pcpp::Packet{rawPacket};
        auto const udp = packet.getLayerOfType<pcpp::UdpLayer>();
        REQUIRE(udp != nullptr);
        REQUIRE(udp->getLayerPayload() != nullptr);
        auto const rtp = std::span<std::uint8_t const>{udp->getLayerPayload(), udp->getLayerPayloadSize()};
        if (udp->getDstPort() == 9875)
        {
            // The fragmented capture includes a SAP announcement; it is not an RTP event.
            auto const announcement = std::string_view{reinterpret_cast<char const*>(rtp.data()), rtp.size()};
            REQUIRE(announcement.find(fmt::format("a=rtpmap:{} ST2110-41/48000", capture.payloadType)) != std::string_view::npos);
            ++announcements;
            continue;
        }
        REQUIRE(packetIndex < expectedPackets);
        auto const fragmentIndex = packetIndex % capture.fragmentSizes.size();
        auto const expectedSize = capture.fragmentSizes[fragmentIndex];
        auto const finalFragment = fragmentIndex + 1 == capture.fragmentSizes.size();
        auto const expectedTimestamp = std::uint64_t{capture.firstTimestamp} + (completedEvents * 1920);
        // All fragments share one timestamp; only a new event advances it by 1,920 ticks (40 ms).
        REQUIRE(rtp.size() == 20 + expectedSize);
        REQUIRE(rtp[1] == capture.payloadType);
        REQUIRE(((std::uint32_t{rtp[2]} << 8) | std::uint32_t{rtp[3]}) == capture.firstSequence + packetIndex);
        REQUIRE(readNetworkWord(rtp.subspan<4, 4>()) == expectedTimestamp);
        auto const itemHeader = readNetworkWord(rtp.subspan<12, 4>());
        REQUIRE((itemHeader >> 10) == capture.dataItemType);
        REQUIRE(((itemHeader >> 9) & 1U) == static_cast<std::uint32_t>(finalFragment));
        REQUIRE((itemHeader & 0x1FFU) == (expectedSize / 4) + 1);
        REQUIRE(std::uint64_t{readNetworkWord(rtp.subspan<16, 4>())} * 4 == originalEvent.size());

        // Consume each entry before publishing the next to exercise slot reuse without an overrun.
        // Comparing against packet-owned storage also avoids using the writer buffer as the oracle.
        writeSt2110Packet(fixture.writer, rtp, taiEpoch, fixture.config.event.eventPayloadSize);
        fixture.checkHead(packetIndex);
        auto info = mxlEventInfo{};
        auto buffer = static_cast<std::uint8_t*>(nullptr);
        REQUIRE(mxlFlowReaderGetEventNonBlocking(fixture.reader, &info, &buffer) == MXL_STATUS_OK);
        REQUIRE(buffer != nullptr);
        REQUIRE(info.version == mxl::lib::EVENT_HEADER_VERSION);
        REQUIRE(info.size == sizeof info);
        REQUIRE(info.timestamp == taiEpoch + (expectedTimestamp * 1'000'000'000 / 48'000));
        REQUIRE(info.flags == 0);
        REQUIRE(info.registryType == MXL_EVENT_REGISTRY_TYPE_SMPTE);
        auto const expectedDit = fmt::format("{:X}", capture.dataItemType);
        REQUIRE(std::string_view{info.dataItemType, expectedDit.size()} == expectedDit);
        REQUIRE(info.dataItemType[expectedDit.size()] == '\0');
        REQUIRE(info.eventSize == expectedSize);
        // The next fragment must start exactly after the bytes already assembled: no gaps or overlaps.
        REQUIRE(info.offset == receivedEvent.size());
        REQUIRE(info.complete == static_cast<std::uint8_t>(finalFragment));
        auto const originalPayload = rtp.subspan(20);
        auto const receivedPayload = std::span<std::uint8_t const>{buffer, info.eventSize};
        REQUIRE(std::ranges::equal(receivedPayload, originalPayload));
        originalEvent.insert(originalEvent.end(), originalPayload.begin(), originalPayload.end());
        receivedEvent.insert(receivedEvent.end(), receivedPayload.begin(), receivedPayload.end());
        if (info.complete)
        {
            // A final fragment closes one logical event. Reset both assemblies before the next one.
            REQUIRE(receivedEvent == originalEvent);
            originalEvent.clear();
            receivedEvent.clear();
            ++completedEvents;
        }
        ++packetIndex;
    }
    // Catch skipped packets, missing final fragments, and unexpected extra queue entries at EOF.
    REQUIRE(packetIndex == expectedPackets);
    REQUIRE(completedEvents == capture.eventCount);
    REQUIRE(announcements == capture.announcementCount);
    REQUIRE(originalEvent.empty());
    REQUIRE(receivedEvent.empty());
    auto info = mxlEventInfo{};
    auto buffer = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(fixture.reader, &info, &buffer) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
}

/** @test Opening an event initializes every metadata field. */
TEST_CASE_METHOD(EventFixture, "Opening an event initializes every metadata field", "[events]")
{
    auto info = mxlEventInfo{};
    auto payload = static_cast<std::uint8_t*>(nullptr);
    // Check the initial open, reopening after cancel, and reopening after commit.
    for (auto attempt = unsigned{0}; attempt < 3; ++attempt)
    {
        CAPTURE(attempt);
        // Poison all fields first so initialization cannot pass by relying on zeroed caller storage.
        std::memset(&info, 0xA5, sizeof info);
        REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
        REQUIRE(payload != nullptr);
        REQUIRE(info.version == mxl::lib::EVENT_HEADER_VERSION);
        REQUIRE(info.size == sizeof info);
        REQUIRE(info.timestamp == 0);
        REQUIRE(info.flags == 0);
        REQUIRE(info.registryType == MXL_EVENT_REGISTRY_TYPE_MXL);
        REQUIRE(std::ranges::all_of(info.dataItemType,
            [](auto value) noexcept
            {
                // Every byte must be reset, including bytes that held metadata from a previous open.
                return value == 0;
            }));
        REQUIRE(info.eventSize == 0);
        REQUIRE(info.index == MXL_UNDEFINED_INDEX);
        REQUIRE(info.offset == 0);
        REQUIRE(info.complete == 1);
        REQUIRE(std::ranges::all_of(info.reserved,
            [](auto value) noexcept
            {
                // Every byte must be reset, including bytes that held metadata from a previous open.
                return value == 0;
            }));

        info.timestamp = 42;
        info.registryType = MXL_EVENT_REGISTRY_TYPE_SMPTE;
        std::strcpy(info.dataItemType, "010203");
        info.eventSize = 1;
        info.complete = 0;
        payload[0] = 0xAB;
        if (attempt == 1)
        {
            REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
        }
        else
        {
            REQUIRE(mxlFlowWriterCancelEvent(writer) == MXL_STATUS_OK);
        }
    }
    checkHead(0);
}

/** @test Event cancellation and invalid commits do not publish. */
TEST_CASE_METHOD(EventFixture, "Event cancellation and invalid commits do not publish", "[events]")
{
    // Only a successful commit may advance the head or expose the private write buffer.
    checkHead(MXL_UNDEFINED_INDEX);
    auto info = mxlEventInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
    checkHead(MXL_UNDEFINED_INDEX);
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_ERR_INVALID_ARG);
    info.eventSize = config.event.eventPayloadSize + 1;
    info.timestamp = 1000;
    REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_ERR_INVALID_ARG);
    checkHead(MXL_UNDEFINED_INDEX);
    auto received = mxlEventInfo{};
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &received, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
    REQUIRE(mxlFlowWriterCancelEvent(writer) == MXL_STATUS_OK);
    checkHead(MXL_UNDEFINED_INDEX);
    REQUIRE(mxlFlowWriterCancelEvent(writer) == MXL_ERR_INVALID_ARG);
    write(42);
    checkHead(0);
    read(42);
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
    info.timestamp = 1000;
    info.eventSize = 0;
    info.complete = 1;
    REQUIRE(mxlFlowWriterCancelEvent(writer) == MXL_STATUS_OK);
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
    checkHead(0);
    info.eventSize = 0;
    info.timestamp = 43;
    info.complete = 1;
    REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
    checkHead(1);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &received, &payload) == MXL_STATUS_OK);
    REQUIRE(received.eventSize == 0);
    REQUIRE(received.offset == 0);
    REQUIRE(received.complete == 1);
}

/** @test Event commits reject earlier timestamps without publishing. */
TEST_CASE_METHOD(EventFixture, "Event commits reject earlier timestamps without publishing", "[events][timestamps]")
{
    auto timestamps = std::vector<std::uint64_t>{500};
    SECTION("Before ring wraparound")
    {}
    SECTION("After ring wraparound")
    {
        for (auto i = std::uint64_t{1}; i < config.event.eventCount + 2; ++i)
        {
            timestamps.push_back(500 + i);
        }
    }
    SECTION("At the maximum timestamp")
    {
        timestamps.front() = std::numeric_limits<std::uint64_t>::max();
    }
    for (auto const timestamp : timestamps)
    {
        write(timestamp);
        read(timestamp);
    }

    // Compare runtime state as well as read results: rejection must not publish a phantom entry.
    auto before = mxlFlowInfo{};
    REQUIRE(mxlFlowReaderGetInfo(reader, &before) == MXL_STATUS_OK);
    auto info = mxlEventInfo{};
    auto payload = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
    info.timestamp = timestamps.back() - 1;
    info.eventSize = 1;
    info.complete = 1;
    payload[0] = 0xAB;
    REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_ERR_INVALID_ARG);

    auto after = mxlFlowInfo{};
    REQUIRE(mxlFlowReaderGetInfo(reader, &after) == MXL_STATUS_OK);
    REQUIRE(after.runtime.headIndex == before.runtime.headIndex);
    REQUIRE(after.runtime.lastWriteTime == before.runtime.lastWriteTime);
    auto received = mxlEventInfo{};
    auto receivedPayload = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &received, &receivedPayload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);

    // A rejected commit retains the transaction and payload for correction.
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_ERR_INVALID_ARG);
    info.timestamp = timestamps.back();
    REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
    checkHead(before.runtime.headIndex + 1);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &received, &receivedPayload) == MXL_STATUS_OK);
    REQUIRE(received.timestamp == timestamps.back());
    REQUIRE(received.eventSize == 1);
    REQUIRE(receivedPayload[0] == 0xAB);
}

/** @test Reopened event writers restore the last committed timestamp. */
TEST_CASE("Reopened event writers restore the last committed timestamp", "[events][timestamps]")
{
    // Keep flow storage alive across writer destruction to check persisted timestamp ordering.
    /** @brief Own the temporary domain used to test writer reattachment without deleting flow files. */
    struct Domain
    {
        std::filesystem::path path = mxl::tests::makeTempDomain(); ///< Directory removed after all local mappings are destroyed.

        /// Remove the test domain without throwing from cleanup.
        ~Domain()
        {
            // Use the error-code overload so cleanup cannot throw during a failed assertion.
            auto error = std::error_code{};
            std::filesystem::remove_all(path, error);
        }
    };

    auto const domain = Domain{};
    auto manager = mxl::lib::FlowManager{domain.path};
    auto const watcher = std::make_shared<mxl::lib::DomainWatcher>(domain.path);
    auto const factory = mxl::lib::PosixFlowIoFactory{watcher};
    auto const parsedFlowId = uuids::uuid::from_string(id);
    if (!parsedFlowId.has_value())
    {
        FAIL("Invalid event test flow UUID.");
        return;
    }
    auto const flowId = parsedFlowId.value();
    auto const createWriter = [&](bool expectedCreated)
    {
        // Reopen the same storage and distinguish attachment from initial flow creation.
        auto [created, data] = manager.createOrOpenEventFlow(flowId, definition, 4, mxlRational{20, 1}, MXL_DATA_FORMAT_GRAIN_SIZE);
        REQUIRE(created == expectedCreated);
        return factory.createEventFlowWriter(manager, flowId, std::move(data));
    };
    auto writer = createWriter(true);
    for (auto timestamp = std::uint64_t{42}; timestamp < 48; ++timestamp)
    {
        auto info = mxlEventInfo{};
        auto payload = static_cast<std::uint8_t*>(nullptr);
        REQUIRE(writer->openEvent(&info, &payload) == MXL_STATUS_OK);
        info.timestamp = timestamp;
        info.eventSize = 0;
        info.complete = 1;
        REQUIRE(writer->commit(info) == MXL_STATUS_OK);
    }
    auto const before = writer->getFlowRuntimeInfo();
    // Destroy the internal writer without the instance deleting the flow files.
    writer.reset();
    writer = createWriter(false);

    auto info = mxlEventInfo{};
    auto payload = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(writer->openEvent(&info, &payload) == MXL_STATUS_OK);
    info.timestamp = 46;
    info.eventSize = 0;
    info.complete = 1;
    REQUIRE(writer->commit(info) == MXL_ERR_INVALID_ARG);
    REQUIRE(writer->getFlowRuntimeInfo().headIndex == before.headIndex);
    REQUIRE(writer->getFlowRuntimeInfo().lastWriteTime == before.lastWriteTime);
    info.timestamp = 47;
    REQUIRE(writer->commit(info) == MXL_STATUS_OK);
    REQUIRE(writer->getFlowRuntimeInfo().headIndex == before.headIndex + 1);
}

/** @test Unfragmented events have zero offset and are complete. */
TEST_CASE_METHOD(EventFixture, "Unfragmented events have zero offset and are complete", "[events][fragmentation]")
{
    auto const message = std::vector<std::uint8_t>{0x00, 0x12, 0xFF, 0x34, 0x56};
    // Independent events always start at zero, regardless of the preceding event's size.
    for (auto const timestamp : {std::uint64_t{1234}, std::uint64_t{5678}})
    {
        writeFragment(timestamp, message, 0, true);
        auto const [info, payload] = readFragment();
        REQUIRE(info.timestamp == timestamp);
        REQUIRE(info.offset == 0);
        REQUIRE(info.complete == 1);
        REQUIRE(info.eventSize == message.size());
        REQUIRE(payload == message);
    }
}

/** @test Two event fragments preserve continuity and reassemble across ring wrap. */
TEST_CASE_METHOD(EventFixture, "Two event fragments preserve continuity and reassemble across ring wrap", "[events][fragmentation]")
{
    // Put the first fragment in the final physical slot and the second in slot zero.
    for (auto i = std::uint64_t{0}; i < config.event.eventCount - 1; ++i)
    {
        write(i);
        read(i);
    }
    auto message = std::vector<std::uint8_t>{};
    // Exceed one slot's payload capacity so the message necessarily needs two entries.
    message.resize(config.event.eventPayloadSize + 13);
    for (auto i = std::size_t{0}; i < message.size(); ++i)
    {
        message[i] = static_cast<std::uint8_t>((i * 37) + 11);
    }
    constexpr auto timestamp = std::uint64_t{123456789};
    auto const split = config.event.eventPayloadSize;
    auto const bytes = std::span<std::uint8_t const>{message};
    writeFragment(timestamp, bytes.first(split), 0, false);
    writeFragment(timestamp, bytes.subspan(split), split, true);

    auto const [first, firstPayload] = readFragment();
    REQUIRE(first.timestamp == timestamp);
    REQUIRE(first.offset == 0);
    REQUIRE(first.complete == 0);
    REQUIRE(first.eventSize == split);
    REQUIRE(std::equal(firstPayload.begin(), firstPayload.end(), bytes.begin()));

    auto const [second, secondPayload] = readFragment();
    REQUIRE(second.timestamp == timestamp);
    REQUIRE(second.offset == split);
    REQUIRE(second.offset == first.offset + first.eventSize);
    REQUIRE(second.complete == 1);
    REQUIRE(second.eventSize == 13);
    REQUIRE(std::equal(secondPayload.begin(), secondPayload.end(), bytes.begin() + split));

    auto assembled = std::vector<std::uint8_t>{firstPayload};
    assembled.insert(assembled.end(), secondPayload.begin(), secondPayload.end());
    REQUIRE(assembled.size() == first.eventSize + second.eventSize);
    REQUIRE(assembled == message);
    checkHead(config.event.eventCount);

    // Completion ends the sequence; the following independent event starts at zero again.
    write(timestamp + 1);
    read(timestamp + 1);
}

/** @test Fragment offset mismatches expose discontinuities to the consumer. */
TEST_CASE_METHOD(EventFixture, "Fragment offset mismatches expose discontinuities to the consumer", "[events][fragmentation]")
{
    auto const firstBytes = std::vector<std::uint8_t>{0x10, 0x20, 0x30, 0x40, 0x50};
    auto const secondBytes = std::vector<std::uint8_t>{0x60, 0x70, 0x80};
    constexpr auto timestamp = std::uint64_t{123456789};
    auto incorrectOffset = std::uint32_t{};
    SECTION("Gap in the payload")
    {
        incorrectOffset = static_cast<std::uint32_t>(firstBytes.size() + 1);
    }
    SECTION("Overlap with the previous fragment")
    {
        incorrectOffset = static_cast<std::uint32_t>(firstBytes.size() - 1);
    }
    SECTION("Unexpected restart at offset zero")
    {
        incorrectOffset = 0;
    }
    writeFragment(timestamp, firstBytes, 0, false);
    writeFragment(timestamp, secondBytes, incorrectOffset, true);

    auto const [first, firstPayload] = readFragment();
    REQUIRE(first.timestamp == timestamp);
    REQUIRE(first.offset == 0);
    REQUIRE(first.complete == 0);
    REQUIRE(first.eventSize == firstBytes.size());
    REQUIRE(firstPayload == firstBytes);

    auto const [second, secondPayload] = readFragment();
    REQUIRE(second.timestamp == first.timestamp);
    REQUIRE(second.offset == incorrectOffset);
    REQUIRE(second.complete == 1);
    REQUIRE(second.eventSize == secondBytes.size());
    REQUIRE(secondPayload == secondBytes);

    // The consumer must validate offset continuity before accepting a completion flag.
    auto const expectedOffset = first.offset + first.eventSize;
    REQUIRE(expectedOffset == 5);
    auto const discontinuity = second.offset != expectedOffset;
    REQUIRE(discontinuity);
    auto const validCompletion = !discontinuity && second.complete == 1;
    REQUIRE_FALSE(validCompletion);

    // Discarding the interrupted assembly does not prevent subsequent delivery.
    write(timestamp + 1);
    read(timestamp + 1);
}

/** @test Event overrun recovers at oldest retained event and hides open slot. */
TEST_CASE_METHOD(EventFixture, "Event overrun recovers at oldest retained event and hides open slot", "[events]")
{
    // Leave the reader idle while ten writes overrun its four-slot history. Only 6..9 survive.
    for (auto i = uint64_t{0}; i < 10; ++i)
    {
        write(i);
        checkHead(i);
    }
    auto info = mxlEventInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
    checkHead(9);
    // Scribbling on an uncommitted write must not damage the oldest still-readable slot.
    std::memset(payload, 255, 32);
    read(6);
    read(7);
    read(8);
    read(9);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
    REQUIRE(mxlFlowWriterCancelEvent(writer) == MXL_STATUS_OK);
    checkHead(9);
    write(10);
    checkHead(10);
    read(10);
}

/** @test Event readers have independent cursors and wake on commit. */
TEST_CASE_METHOD(EventFixture, "Event readers have independent cursors and wake on commit", "[events]")
{
    // A second instance provides an independent cursor; both readers must receive event 100.
    auto other = mxlCreateInstance(domain.c_str(), "{}");
    REQUIRE(other);
    auto otherReader = mxlFlowReader{};
    REQUIRE(mxlCreateFlowReader(other, id, "", &otherReader) == MXL_STATUS_OK);
    auto future = std::async(std::launch::async,
        [&]
        {
            // Wait on an empty queue so the next commit must wake the blocked reader.
            auto info = mxlEventInfo{};
            auto payload = static_cast<uint8_t*>(nullptr);
            auto status = mxlFlowReaderGetEvent(reader, 1000000000, &info, &payload);
            return std::pair{status, info.timestamp};
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    write(100);
    auto result = future.get();
    REQUIRE(result.first == MXL_STATUS_OK);
    REQUIRE(result.second == 100);
    auto info = mxlEventInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(otherReader, &info, &payload) == MXL_STATUS_OK);
    REQUIRE(info.timestamp == 100);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
    REQUIRE(mxlReleaseFlowReader(other, otherReader) == MXL_STATUS_OK);
    REQUIRE(mxlDestroyInstance(other) == MXL_STATUS_OK);
}

/** @test Event API rejects wrong handles and invalid arguments. */
TEST_CASE_METHOD(EventFixture, "Event API rejects wrong handles and invalid arguments", "[events]")
{
    // Reject malformed calls and grain APIs without making the event flow unusable.
    auto info = mxlEventInfo{};
    auto grain = mxlGrainInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowWriterOpenEvent(writer, nullptr, &payload) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, nullptr) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlFlowWriterOpenEvent(nullptr, &info, &payload) == MXL_ERR_INVALID_FLOW_WRITER);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(nullptr, &info, &payload) == MXL_ERR_INVALID_FLOW_READER);
    REQUIRE(mxlFlowWriterOpenGrain(writer, 0, &grain, &payload) == MXL_ERR_INVALID_FLOW_WRITER);
    REQUIRE(mxlFlowReaderGetGrainNonBlocking(reader, 0, &grain, &payload) == MXL_ERR_INVALID_FLOW_READER);
    write(12);
    read(12);
}

/** @test Event parser requires a positive grain rate. */
TEST_CASE("Event parser requires a positive grain rate", "[events]")
{
    // Check event classification and reject descriptors that cannot yield a valid ring capacity.
    REQUIRE(mxlIsSupportedDataFormat(MXL_DATA_FORMAT_EVENT));
    REQUIRE_FALSE(mxlIsDiscreteDataFormat(MXL_DATA_FORMAT_EVENT));
    REQUIRE_FALSE(mxlIsContinuousDataFormat(MXL_DATA_FORMAT_EVENT));
    auto const parser = mxl::lib::FlowParser{definition};
    REQUIRE(parser.getFormat() == MXL_DATA_FORMAT_EVENT);
    REQUIRE(parser.getGrainRate().numerator == 20);
    REQUIRE(parser.getGrainRate().denominator == 1);
    REQUIRE(parser.getPayloadSize() == MXL_DATA_FORMAT_GRAIN_SIZE);
    REQUIRE_THROWS(parser.getTotalPayloadSlices());
    for (auto rate : {R"({"numerator":0})", R"({"numerator":-1})", R"({"numerator":20,"denominator":0})"})
    {
        auto flowDef = definitionWithRate(rate);
        REQUIRE_THROWS(mxl::lib::FlowParser{flowDef});
    }

    auto flow = picojson::value{};
    REQUIRE(picojson::parse(flow, definition).empty());
    flow.get<picojson::object>().erase("grain_rate");
    REQUIRE_THROWS(mxl::lib::FlowParser{flow.serialize()});
}

/** @test Event group hints reject empty components and invalid scopes while accepting supported forms. */
TEST_CASE("Event group hints validate names roles and scopes", "[events][parser]")
{
    // Change only the group hint to isolate valid forms from malformed names, roles and scopes.
    auto flow = picojson::value{};
    REQUIRE(picojson::parse(flow, definition).empty());
    auto& hints = flow.get<picojson::object>()["tags"].get<picojson::object>()["urn:x-nmos:tag:grouphint/v1.0"];

    for (auto const hint : {"group:role", "group:role:device", "group:role:node"})
    {
        CAPTURE(hint);
        hints = picojson::value{picojson::array{picojson::value{std::string{hint}}}};
        REQUIRE_NOTHROW(mxl::lib::FlowParser{flow.serialize()});
    }
    for (auto const hint : {"", ":", "group", "group:", ":role", "group::node", "group:role:", "group:role:invalid", "group:role:node:extra"})
    {
        CAPTURE(hint);
        hints = picojson::value{picojson::array{picojson::value{std::string{hint}}}};
        REQUIRE_THROWS_AS(mxl::lib::FlowParser{flow.serialize()}, std::domain_error);
    }
    hints = picojson::value{picojson::array{}};
    REQUIRE_THROWS_AS(mxl::lib::FlowParser{flow.serialize()}, std::domain_error);
    hints = picojson::value{picojson::array{picojson::value{true}}};
    REQUIRE_THROWS_AS(mxl::lib::FlowParser{flow.serialize()}, std::domain_error);
}

/** @test Event capacity follows grain rate and history duration. */
TEST_CASE("Event capacity follows grain rate and history duration", "[events]")
{
    SECTION("A different rate changes capacity")
    {
        // At 50 entries/s, the default 200 ms history requires ten slots.
        auto fixture = EventFixture{definitionWithRate(R"({"numerator":50})")};
        REQUIRE(fixture.config.event.eventCount == 10);
        REQUIRE(fixture.config.common.grainRate.numerator == 50);
    }
    SECTION("Fractional rates use integer division")
    {
        // 22.5 entries/s over 0.2 s gives 4.5 entries; capacity currently truncates to four.
        auto fixture = EventFixture{definitionWithRate(R"({"numerator":45,"denominator":2})")};
        REQUIRE(fixture.config.event.eventCount == 4);
        REQUIRE(fixture.config.common.grainRate.numerator == 45);
        REQUIRE(fixture.config.common.grainRate.denominator == 2);
    }
    SECTION("Large rational components do not overflow intermediate products")
    {
        // Just over 20 entries/s; coprime components keep both products wider than 64 bits.
        auto fixture = EventFixture{definitionWithRate(R"({"numerator":2000000000001,"denominator":100000000000})")};
        REQUIRE(fixture.config.event.eventCount == 4);
    }
    SECTION("Domain history duration changes capacity")
    {
        // Holding the rate at 20 entries/s and extending history to 0.5 s produces ten slots.
        auto fixture = EventFixture{definition, R"({"urn:x-mxl:option:history_duration/v1.0":500000000})"};
        REQUIRE(fixture.config.event.eventCount == 10);
    }
}

/** @test Event capacity rejects too little or excessive history. */
TEST_CASE_METHOD(EventFixture, "Event capacity rejects too little or excessive history", "[events]")
{
    // These rates request zero, one or more than 65,536 slots with the default 200 ms history.
    for (auto rate : {R"({"numerator":1})", R"({"numerator":5})", R"({"numerator":327685})"})
    {
        auto flowDef = definitionWithRate(rate);
        auto rejected = mxlFlowWriter{};
        REQUIRE(mxlCreateFlowWriter(producer, flowDef.c_str(), nullptr, &rejected, nullptr, nullptr) != MXL_STATUS_OK);
    }
}

/** @test Reject capacities wider than size_t before narrowing to the ring geometry. */
TEST_CASE("Event capacity rejects overflow before narrowing", "[events]")
{
    auto domain = mxl::tests::makeTempDomain();
    {
        auto options = std::ofstream{mxl::lib::makeDomainOptionsFilePath(domain)};
        options << R"({"urn:x-mxl:option:history_duration/v1.0":9223372036854775808})";
    }
    auto instance = mxlCreateInstance(domain.c_str(), nullptr);
    REQUIRE(instance != nullptr);
    auto writer = mxlFlowWriter{};
    // The capacity exceeds 2^64; narrowing first would incorrectly accept 2,361 slots.
    auto flowDef = definitionWithRate(R"({"numerator":4000000000000000512,"denominator":2000000000})");
    auto status = mxlCreateFlowWriter(instance, flowDef.c_str(), nullptr, &writer, nullptr, nullptr);
    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
    std::filesystem::remove_all(domain);
    REQUIRE(status != MXL_STATUS_OK);
}

/** @test Late event reader starts at oldest retained event and detects deleted flow. */
TEST_CASE_METHOD(EventFixture, "Late event reader starts at oldest retained event and detects deleted flow", "[events]")
{
    // Reattach after the four-slot ring has wrapped; the new cursor starts at retained entry 2.
    for (auto i = uint64_t{0}; i < 6; ++i)
    {
        write(i);
    }
    REQUIRE(mxlReleaseFlowReader(consumer, reader) == MXL_STATUS_OK);
    reader = nullptr;
    REQUIRE(mxlCreateFlowReader(consumer, id, "", &reader) == MXL_STATUS_OK);
    read(2);
    read(3);
    read(4);
    read(5);
    REQUIRE(mxlReleaseFlowWriter(producer, writer) == MXL_STATUS_OK);
    writer = nullptr;
    auto info = mxlEventInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_FLOW_INVALID);
}

/** @test Repeated event writer acquisition shares publication state. */
TEST_CASE_METHOD(EventFixture, "Repeated event writer acquisition shares publication state", "[events]")
{
    // Releasing a second reference must leave the original writer and its publication state intact.
    auto same = mxlFlowWriter{};
    auto created = true;
    REQUIRE(mxlCreateFlowWriter(producer, definition, "", &same, nullptr, &created) == MXL_STATUS_OK);
    REQUIRE_FALSE(created);
    REQUIRE(same == writer);
    write(1);
    REQUIRE(mxlReleaseFlowWriter(producer, same) == MXL_STATUS_OK);
    read(1);
    write(2);
    read(2);
}

/** @test Event reader rejects truncated shared payload. */
TEST_CASE_METHOD(EventFixture, "Event reader rejects truncated shared payload", "[events]")
{
    REQUIRE(mxlReleaseFlowReader(consumer, reader) == MXL_STATUS_OK);
    reader = nullptr;
    auto const eventFile = mxl::lib::makeEventDataFilePath(mxl::lib::makeFlowDirectoryName(domain, id));
    // Removing even the final byte makes the last slot incomplete.
    std::filesystem::resize_file(eventFile, std::filesystem::file_size(eventFile) - 1);
    REQUIRE(mxlCreateFlowReader(consumer, id, "", &reader) != MXL_STATUS_OK);
}

/** @test Event slots occupy one shared memory file. */
TEST_CASE_METHOD(EventFixture, "Event slots occupy one shared memory file", "[events]")
{
    auto const flowDirectory = mxl::lib::makeFlowDirectoryName(domain, id);
    auto const eventFile = mxl::lib::makeEventDataFilePath(flowDirectory);
    REQUIRE(std::filesystem::is_regular_file(eventFile));
    REQUIRE(config.event.eventPayloadSize == 4096);
    // One 4 KiB ring header precedes slots containing 640 bytes of internal metadata plus payload.
    REQUIRE(std::filesystem::file_size(eventFile) == 4096 + (config.event.eventCount * 4736));
    REQUIRE(std::filesystem::file_size(eventFile) == mxl::lib::EventRingBuffer::bufferSize(config.event.eventCount, config.event.eventPayloadSize));
    REQUIRE_FALSE(std::filesystem::exists(flowDirectory / "grains"));
    REQUIRE_FALSE(std::filesystem::exists(flowDirectory / "producer"));

    for (auto i = uint64_t{0}; i < 6; ++i)
    {
        auto info = mxlEventInfo{};
        auto payload = static_cast<uint8_t*>(nullptr);
        REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
        info.timestamp = i;
        info.eventSize = sizeof i;
        info.complete = 1;
        std::memcpy(payload, &i, sizeof i);
        REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
    }

    auto info = mxlEventInfo{};
    auto payload = static_cast<uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    read(2);
    read(3);
    read(4);
    read(5);
}

/** @test Event reader validates headers throughout the shared buffer. */
TEST_CASE_METHOD(EventFixture, "Event reader validates headers throughout the shared buffer", "[events]")
{
    REQUIRE(mxlReleaseFlowReader(consumer, reader) == MXL_STATUS_OK);
    reader = nullptr;
    auto const eventFile = mxl::lib::makeEventDataFilePath(mxl::lib::makeFlowDirectoryName(domain, id));
    auto const slotStride = mxl::lib::EventRingBuffer::slotStride(config.event.eventPayloadSize);
    {
        auto file = std::fstream{eventFile, std::ios::in | std::ios::out | std::ios::binary};
        REQUIRE(file.is_open());
        // Corrupt the last slot so validation must inspect more than the first header.
        file.seekp(static_cast<std::streamoff>(sizeof(mxl::lib::EventRingHeader) + ((config.event.eventCount - 1) * slotStride)));
        auto const invalidVersion = uint32_t{0};
        file.write(reinterpret_cast<char const*>(&invalidVersion), sizeof invalidVersion);
        REQUIRE(file.good());
    }
    REQUIRE(mxlCreateFlowReader(consumer, id, "", &reader) != MXL_STATUS_OK);
}

namespace
{
    /**
     * @brief Publish a deterministic variable-length payload used to detect torn snapshots.
     * @param writer Event producer with no open transaction.
     * @param value Timestamp and seed for the payload length and byte pattern.
     * @return Result of opening or committing the event.
     */
    mxlStatus writePattern(mxlFlowWriter writer, std::uint64_t value)
    {
        // Vary length and byte values with the timestamp to expose partially overwritten snapshots.
        auto info = mxlEventInfo{};
        auto payload = static_cast<std::uint8_t*>(nullptr);
        auto status = mxlFlowWriterOpenEvent(writer, &info, &payload);
        if (status != MXL_STATUS_OK)
        {
            return status;
        }
        info.timestamp = value;
        info.eventSize = value % (MXL_DATA_FORMAT_GRAIN_SIZE + 1);
        info.offset = 0;
        info.complete = 1;
        for (auto i = std::uint32_t{0}; i < info.eventSize; ++i)
        {
            payload[i] = static_cast<std::uint8_t>((value * 29) + (std::uint64_t{i} * 7));
        }
        return mxlFlowWriterCommitEvent(writer, &info);
    }

    /**
     * @brief Validate a successful read against the timestamp-derived payload pattern.
     * @param info Metadata from a successful event read.
     * @param payload Snapshot bytes covering info.eventSize.
     * @return True if all expected metadata and payload bytes match.
     */
    bool validPattern(mxlEventInfo const& info, std::uint8_t const* payload)
    {
        // Recompute the expected bytes from metadata instead of trusting the writer buffer.
        if (info.version != mxl::lib::EVENT_HEADER_VERSION || info.size != sizeof info || info.flags != 0 || info.offset != 0 || info.complete != 1 ||
            info.eventSize != info.timestamp % (MXL_DATA_FORMAT_GRAIN_SIZE + 1))
        {
            return false;
        }
        for (auto i = std::uint32_t{0}; i < info.eventSize; ++i)
        {
            if (payload[i] != static_cast<std::uint8_t>((info.timestamp * 29) + (std::uint64_t{i} * 7)))
            {
                return false;
            }
        }
        return true;
    }
}

/** @test Independent readers each consume every event without duplicate delivery. */
TEST_CASE("Independent event readers each consume every publication", "[events][concurrency]")
{
    auto fixture = EventFixture{definitionWithRate(R"({"numerator":2560})")};
    constexpr auto total = std::size_t{256};
    auto consumers = std::vector<std::unique_ptr<IndependentEventReader>>{};
    for (auto i = 0; i < 4; ++i)
    {
        consumers.push_back(std::make_unique<IndependentEventReader>(fixture.domain));
    }
    auto start = std::barrier{6};
    auto failed = std::atomic<bool>{false};
    auto producer = std::async(std::launch::async,
        [&]
        {
            start.arrive_and_wait();
            for (auto i = std::size_t{0}; i < total; ++i)
            {
                if (writePattern(fixture.writer, 4000 + i) != MXL_STATUS_OK)
                {
                    failed = true;
                    return false;
                }
            }
            return true;
        });
    auto readers = std::vector<std::future<std::vector<std::uint64_t>>>{};
    for (auto const& consumer : consumers)
    {
        readers.emplace_back(std::async(std::launch::async,
            [&, reader = consumer->reader]
            {
                auto values = std::vector<std::uint64_t>{};
                start.arrive_and_wait();
                auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
                while (!failed && (values.size() < total) && (std::chrono::steady_clock::now() < deadline))
                {
                    auto info = mxlEventInfo{};
                    auto payload = static_cast<std::uint8_t*>(nullptr);
                    auto const status = mxlFlowReaderGetEvent(reader, 1000000, &info, &payload);
                    if (status == MXL_STATUS_OK)
                    {
                        std::this_thread::yield();
                        if (!validPattern(info, payload) || (info.index != values.size()))
                        {
                            failed = true;
                            break;
                        }
                        values.push_back(info.timestamp);
                    }
                    else if (status != MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                    {
                        failed = true;
                        break;
                    }
                }
                return values;
            }));
    }
    start.arrive_and_wait();
    auto const successfulWrites = producer.get();
    auto results = std::vector<std::vector<std::uint64_t>>{};
    for (auto& future : readers)
    {
        results.push_back(future.get());
    }
    REQUIRE(successfulWrites);
    REQUIRE_FALSE(failed);
    for (auto const& values : results)
    {
        REQUIRE(values.size() == total);
        for (auto i = std::size_t{0}; i < total; ++i)
        {
            REQUIRE(values[i] == 4000 + i);
        }
    }
    fixture.checkHead(total - 1);
}

/** @test Event snapshots survive overwrite and reads from independent readers. */
TEST_CASE_METHOD(EventFixture, "Event snapshots survive overwrite and reads from independent readers", "[events][concurrency]")
{
    // Retain this thread's snapshot while an independent reader repeatedly reads and overwrites the ring.
    REQUIRE(writePattern(writer, 4095) == MXL_STATUS_OK);
    auto info = mxlEventInfo{};
    auto payload = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
    auto other = IndependentEventReader{domain};
    auto future = std::async(std::launch::async,
        [&]
        {
            // Overwrite the ring repeatedly while the originating thread retains its earlier snapshot.
            auto initialInfo = mxlEventInfo{};
            auto initialPayload = static_cast<std::uint8_t*>(nullptr);
            if (mxlFlowReaderGetEventNonBlocking(other.reader, &initialInfo, &initialPayload) != MXL_STATUS_OK)
            {
                return false;
            }
            for (auto i = std::uint64_t{0}; i < 100; ++i)
            {
                if (writePattern(writer, 4096 + i) != MXL_STATUS_OK)
                {
                    return false;
                }
                auto otherInfo = mxlEventInfo{};
                auto otherPayload = static_cast<std::uint8_t*>(nullptr);
                if (mxlFlowReaderGetEventNonBlocking(other.reader, &otherInfo, &otherPayload) != MXL_STATUS_OK ||
                    !validPattern(otherInfo, otherPayload))
                {
                    return false;
                }
            }
            return true;
        });
    REQUIRE(future.get());
    REQUIRE(validPattern(info, payload));
}

/** @test Event readers never expose torn payloads during concurrent wraparound. */
TEST_CASE_METHOD(EventFixture, "Event readers never expose torn payloads during concurrent wraparound", "[events][concurrency]")
{
    // Independent-reader stress with a tiny ring: overruns and empty reads are expected.
    // Every successful read must still contain matching metadata and timestamp-derived bytes.
    auto consumers = std::vector<std::unique_ptr<IndependentEventReader>>{};
    for (auto i = 0; i < 2; ++i)
    {
        consumers.push_back(std::make_unique<IndependentEventReader>(domain));
    }
    auto start = std::barrier{4};
    auto finished = std::atomic<bool>{false};
    auto failures = std::atomic<unsigned>{0};
    auto snapshots = std::atomic<unsigned>{0};
    auto workers = std::vector<std::future<void>>{};
    workers.emplace_back(std::async(std::launch::async,
        [&]
        {
            // Keep wrapping the small ring to race publication against readers copying payloads.
            start.arrive_and_wait();
            for (auto i = std::uint64_t{0}; i < 6000; ++i)
            {
                if (writePattern(writer, i) != MXL_STATUS_OK)
                {
                    ++failures;
                    break;
                }
            }
            finished = true;
        }));
    for (auto thread = unsigned{0}; thread < 2; ++thread)
    {
        workers.emplace_back(std::async(std::launch::async,
            [&, eventReader = consumers[thread]->reader]
            {
                // Accept missed entries but reject any successful read containing inconsistent bytes.
                start.arrive_and_wait();
                auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
                do
                {
                    auto info = mxlEventInfo{};
                    auto payload = static_cast<std::uint8_t*>(nullptr);
                    auto const status = mxlFlowReaderGetEvent(eventReader, 1000000, &info, &payload);
                    if (status == MXL_STATUS_OK)
                    {
                        std::this_thread::yield();
                        if (!validPattern(info, payload))
                        {
                            ++failures;
                        }
                        ++snapshots;
                    }
                    else if (status != MXL_ERR_OUT_OF_RANGE_TOO_EARLY && status != MXL_ERR_OUT_OF_RANGE_TOO_LATE)
                    {
                        ++failures;
                    }
                    if (finished && status == MXL_ERR_OUT_OF_RANGE_TOO_EARLY)
                    {
                        return;
                    }
                }
                while (std::chrono::steady_clock::now() < deadline);
                ++failures;
            }));
    }
    start.arrive_and_wait();
    for (auto& future : workers)
    {
        future.get();
    }
    REQUIRE(failures == 0);
    REQUIRE(snapshots > 0);
    checkHead(5999);
}

/** @test One event writer wakes independent readers in other processes. */
TEST_CASE("One event writer wakes independent readers in other processes", "[events][concurrency][ipc]")
{
    // Spawn separate processes to verify wakeups and payload visibility across shared mappings.
    /** @brief Own the pipe descriptors used to coordinate child readers with the producer. */
    struct Pipe
    {
        std::array<int, 2> fds{-1, -1}; ///< Read and write descriptors, each -1 until initialized.

        /// Close any initialized descriptors during normal or failed test teardown.
        ~Pipe()
        {
            // Close only initialized descriptors when setup or an assertion fails.
            for (auto fd : fds)
            {
                if (fd >= 0)
                {
                    ::close(fd);
                }
            }
        }
    };

    /** @brief Ensure a child reader cannot outlive a failed IPC test. */
    struct Child
    {
        pid_t pid{-1}; ///< Child process ID, or -1 after it has been reaped.

        /// Kill and reap an outstanding child during test teardown.
        ~Child()
        {
            if (pid > 0)
            {
                // Ensure a failed assertion cannot leave a blocked child behind.
                ::kill(pid, SIGKILL);
                while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR)
                {
                }
            }
        }
    };

    auto fixture = EventFixture{definitionWithRate(R"({"numerator":2560})")};
    auto ready = Pipe{};
    auto start = Pipe{};
    // The child readers inherit these descriptors across exec to coordinate with the parent.
    REQUIRE(::pipe(ready.fds.data()) == 0); // NOLINT(android-cloexec-pipe): readiness descriptor must survive exec.
    REQUIRE(::pipe(start.fds.data()) == 0); // NOLINT(android-cloexec-pipe): start descriptor must survive exec.
    auto children = std::array<Child, 2>{};
    auto environment = std::array<char*, 1>{nullptr};
    for (auto i = std::size_t{0}; i < children.size(); ++i)
    {
        auto arguments = std::array<std::string, 7>{
            MXL_EVENT_IPC_WORKER, fixture.domain.string(), id, "4000", "256", std::to_string(ready.fds[1]), std::to_string(start.fds[0])};
        auto argv = std::array<char*, 8>{};
        for (auto arg = std::size_t{0}; arg < arguments.size(); ++arg)
        {
            argv[arg] = arguments[arg].data();
        }
        REQUIRE(::posix_spawn(&children[i].pid, argv[0], nullptr, nullptr, argv.data(), environment.data()) == 0);
    }
    // Both child readers attach before the parent publishes.
    for (auto i = std::size_t{0}; i < children.size(); ++i)
    {
        auto watched = pollfd{.fd = ready.fds[0], .events = POLLIN, .revents = 0};
        REQUIRE(::poll(&watched, 1, 10000) == 1);
        auto token = char{};
        REQUIRE(::read(ready.fds[0], &token, 1) == 1);
        REQUIRE(token == 'R');
    }
    REQUIRE(::write(start.fds[1], "GG", 2) == 2);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    for (auto i = std::uint64_t{0}; i < 256; ++i)
    {
        REQUIRE(writePattern(fixture.writer, 4000 + i) == MXL_STATUS_OK);
    }
    for (auto i = std::uint64_t{0}; i < 256; ++i)
    {
        auto info = mxlEventInfo{};
        auto payload = static_cast<std::uint8_t*>(nullptr);
        REQUIRE(mxlFlowReaderGetEventNonBlocking(fixture.reader, &info, &payload) == MXL_STATUS_OK);
        REQUIRE(info.timestamp == 4000 + i);
        REQUIRE(validPattern(info, payload));
    }
    for (auto& child : children)
    {
        auto status = int{};
        auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        auto result = pid_t{0};
        do
        {
            result = ::waitpid(child.pid, &status, WNOHANG);
            if (result != 0)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        while (std::chrono::steady_clock::now() < deadline);
        REQUIRE(result == child.pid);
        child.pid = -1;
        REQUIRE(WIFEXITED(status));
        REQUIRE(WEXITSTATUS(status) == 0);
    }
    fixture.checkHead(255);
    // Child reader teardown leaves the sole writer usable.
    fixture.write(9000);
    fixture.read(9000);
}

/** @test Compact event slots preserve adjacent entries and buffer bounds for unaligned payload capacities. */
TEST_CASE("Compact event slots preserve payload boundaries across wraparound", "[events]")
{
    using Ring = mxl::lib::EventRingBuffer;

    /** @brief Aligned backing storage with an extra block to detect writes beyond the ring. */
    struct alignas(64) Block
    {
        std::uint8_t bytes[64]; ///< Raw storage for slots or the trailing sentinel.
    };

    constexpr auto count = std::uint32_t{3};
    for (auto const capacity : std::array<std::uint32_t, 8>{1, 7, 8, 13, 63, 64, 65, 4096})
    {
        // Probe word and cache-line boundaries; the extra block detects writes beyond the ring.
        CAPTURE(capacity);
        auto const size = Ring::bufferSize(count, capacity);
        REQUIRE(size % sizeof(Block) == 0);
        auto memory = std::vector<Block>{(size / sizeof(Block)) + 1};
        std::memset(memory.back().bytes, 0xCD, sizeof(Block));
        Ring::initialize(memory.data(), count, capacity);
        auto ring = Ring{memory.data(), count, capacity};
        auto payload = std::vector<std::uint8_t>{};
        payload.resize(capacity);
        for (auto index = std::uint64_t{0}; index < std::uint64_t{count} * 2; ++index)
        {
            auto info = mxlEventInfo{};
            info.version = mxl::lib::EVENT_HEADER_VERSION;
            info.size = sizeof info;
            info.timestamp = index;
            info.eventSize = capacity;
            info.complete = 1;
            std::ranges::fill(payload, static_cast<std::uint8_t>(index));
            REQUIRE(ring.publish(index, info, payload.data()) == MXL_STATUS_OK);
        }
        REQUIRE_NOTHROW(Ring{memory.data(), count, capacity});
        for (auto index = std::uint64_t{count}; index < std::uint64_t{count} * 2; ++index)
        {
            auto info = mxlEventInfo{};
            std::ranges::fill(payload, 0);
            REQUIRE(ring.read(index, info, payload.data()) == Ring::ReadResult::Ready);
            REQUIRE(info.timestamp == index);
            REQUIRE(info.eventSize == capacity);
            REQUIRE(std::ranges::all_of(payload,
                [index](auto byte) noexcept
                {
                    // Each retained slot must contain only the byte pattern written for its own index.
                    return byte == index;
                }));
        }
        auto const sentinel = std::span{memory.back().bytes};
        REQUIRE(std::ranges::all_of(sentinel,
            [](auto byte) noexcept
            {
                // An unchanged canary means no write reached the block beyond the ring.
                return byte == 0xCD;
            }));
    }
}

/** @test Event atomic copies survive contention within the same mapping. */
TEST_CASE("Event atomic copies survive contention within the same mapping", "[events][concurrency]")
{
    // Race low-level copies in one mapping: overwritten entries are allowed, torn snapshots are not.
    using Ring = mxl::lib::EventRingBuffer;

    /** @brief Page-aligned backing storage for an in-memory event ring test. */
    struct alignas(4096) Page
    {
        std::uint8_t bytes[4096]; ///< One page of raw mapping storage.
    };

    constexpr auto count = std::uint32_t{3};
    constexpr auto total = std::uint64_t{4000};
    auto memory = std::vector<Page>{(Ring::bufferSize(count, 4096) + sizeof(Page) - 1) / sizeof(Page)};
    Ring::initialize(memory.data(), count, 4096);
    auto ring = Ring{memory.data(), count, 4096};
    auto start = std::barrier{3};
    auto failed = std::atomic<bool>{false};
    auto producer = std::async(std::launch::async,
        [&]
        {
            // Vary both length and contents while publishing directly into the shared mapping.
            start.arrive_and_wait();
            for (auto index = std::uint64_t{0}; index < total && !failed; ++index)
            {
                auto info = mxlEventInfo{};
                info.version = mxl::lib::EVENT_HEADER_VERSION;
                info.size = sizeof info;
                info.timestamp = 4000 + index;
                info.eventSize = info.timestamp % 4097;
                info.complete = 1;
                auto payload = std::array<std::uint8_t, 4096>{};
                for (auto byte = std::uint32_t{0}; byte < info.eventSize; ++byte)
                {
                    payload[byte] = static_cast<std::uint8_t>((info.timestamp * 29) + (std::uint64_t{byte} * 7));
                }
                if (ring.publish(index, info, payload.data()) != MXL_STATUS_OK)
                {
                    failed = true;
                }
            }
        });
    auto reader = std::async(std::launch::async,
        [&]
        {
            // Skip overwritten indices and validate every snapshot that the ring reports as ready.
            start.arrive_and_wait();
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
            auto index = std::uint64_t{0};
            auto received = std::size_t{0};
            while (index < total && !failed && std::chrono::steady_clock::now() < deadline)
            {
                auto info = mxlEventInfo{};
                auto payload = std::array<std::uint8_t, 4096>{};
                auto const result = ring.read(index, info, payload.data());
                if (result == Ring::ReadResult::Ready)
                {
                    if (info.timestamp != 4000 + index || !validPattern(info, payload.data()))
                    {
                        failed = true;
                        break;
                    }
                    ++received;
                    ++index;
                }
                else if (result == Ring::ReadResult::Overwritten)
                {
                    ++index;
                }
                else if (result == Ring::ReadResult::Invalid)
                {
                    failed = true;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            if (index != total)
            {
                failed = true;
            }
            return received;
        });
    start.arrive_and_wait();
    producer.get();
    auto const received = reader.get();
    REQUIRE_FALSE(failed);
    REQUIRE(received > 0);
}

/** @test Event sequence exhaustion cannot alias an empty slot. */
TEST_CASE("Event sequence exhaustion cannot alias an empty slot", "[events]")
{
    using Ring = mxl::lib::EventRingBuffer;

    /** @brief Page-aligned backing storage for an in-memory event ring test. */
    struct alignas(4096) Page
    {
        std::uint8_t bytes[4096]; ///< One page of raw mapping storage.
    };

    auto memory = std::vector<Page>{(Ring::bufferSize(2, 13) + sizeof(Page) - 1) / sizeof(Page)};
    Ring::initialize(memory.data(), 2, 13);
    auto ring = Ring{memory.data(), 2, 13};
    auto const index = Ring::MAX_INDEX;
    // The largest valid sequence must stay distinguishable from the empty-slot sentinel.
    auto info = mxlEventInfo{};
    info.version = mxl::lib::EVENT_HEADER_VERSION;
    info.size = sizeof info;
    info.eventSize = 13;
    auto payload = std::array<std::uint8_t, 13>{};
    payload.fill(0xAB);
    REQUIRE(ring.publish(index, info, payload.data()) == MXL_STATUS_OK);
    payload.fill(0);
    REQUIRE(ring.read(index, info, payload.data()) == Ring::ReadResult::Ready);
    REQUIRE(std::ranges::all_of(payload,
        [](auto byte) noexcept
        {
            // The final valid sequence must still deliver every byte of its payload.
            return byte == 0xAB;
        }));
    REQUIRE(ring.publish(index + 1, info, payload.data()) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    REQUIRE(ring.read(Ring::MAX_INDEX + 1, info, payload.data()) == Ring::ReadResult::Pending);
}

/** @test Interrupted copies may be retried, but completed sequence tags must never be reused. */
TEST_CASE("Event publication retries only unpublished sequence tags", "[events][recovery]")
{
    using Ring = mxl::lib::EventRingBuffer;

    struct alignas(4096) Page
    {
        std::uint8_t bytes[4096];
    };

    auto memory = std::vector<Page>{(Ring::bufferSize(2, 13) + sizeof(Page) - 1) / sizeof(Page)};
    Ring::initialize(memory.data(), 2, 13);
    auto ring = Ring{memory.data(), 2, 13};
    auto slot = reinterpret_cast<mxl::lib::Event*>(reinterpret_cast<std::uint8_t*>(memory.data()) + sizeof(mxl::lib::EventRingHeader));
    auto info = mxlEventInfo{};
    info.version = mxl::lib::EVENT_HEADER_VERSION;
    info.size = sizeof info;
    info.eventSize = 13;
    auto payload = std::array<std::uint8_t, 13>{};
    payload.fill(0xAB);
    SECTION("Unpublished tag may be retried")
    {
        slot->header.sequence = 0;
        REQUIRE(ring.read(0, info, payload.data()) == Ring::ReadResult::Pending);
        REQUIRE(ring.publish(0, info, payload.data()) == MXL_STATUS_OK);
        payload.fill(0);
        REQUIRE(ring.read(0, info, payload.data()) == Ring::ReadResult::Ready);
        REQUIRE(std::ranges::all_of(payload, [](auto byte) { return byte == 0xAB; }));
        REQUIRE(ring.publish(0, info, payload.data()) == MXL_ERR_FLOW_INVALID);
    }
    SECTION("Completed tag may not be reused")
    {
        REQUIRE(ring.publish(0, info, payload.data()) == MXL_STATUS_OK);
        REQUIRE(ring.publish(0, info, payload.data()) == MXL_ERR_FLOW_INVALID);
    }
    SECTION("Newer writing tag may not be overwritten")
    {
        slot->header.sequence = 2 << 1;
        REQUIRE(ring.publish(0, info, payload.data()) == MXL_ERR_FLOW_INVALID);
    }
}

/** @test Queue indices reveal lag and losses independently of timestamps. */
TEST_CASE_METHOD(EventFixture, "Event indices identify queue position and skipped entries", "[events]")
{
    write(42);
    auto info = mxlEventInfo{};
    auto payload = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
    REQUIRE(info.index == 0);
    auto const previous = info.index;
    for (auto i = 0; i < 6; ++i)
    {
        write(42); // Identical timestamps cannot identify queue position.
    }
    auto const before = info;
    auto const beforePayload = payload;
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_LATE);
    REQUIRE(std::memcmp(&before, &info, sizeof info) == 0);
    REQUIRE(payload == beforePayload);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
    REQUIRE(info.index == 3);
    REQUIRE(info.index - previous - 1 == 2);
    auto runtime = mxlFlowRuntimeInfo{};
    REQUIRE(mxlFlowReaderGetRuntimeInfo(reader, &runtime) == MXL_STATUS_OK);
    REQUIRE(runtime.headIndex - info.index == 3);
    for (auto i = std::uint64_t{4}; i <= 6; ++i)
    {
        REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
        REQUIRE(info.index == i);
    }
    REQUIRE(mxlFlowWriterOpenEvent(writer, &info, &payload) == MXL_STATUS_OK);
    REQUIRE(info.eventSize == 0);
    REQUIRE(info.index == MXL_UNDEFINED_INDEX);
    info.timestamp = 42;
    info.index = 999; // Producer-supplied queue positions are ignored.
    REQUIRE(mxlFlowWriterCommitEvent(writer, &info) == MXL_STATUS_OK);
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_STATUS_OK);
    REQUIRE(info.index == 7);
    REQUIRE(info.eventSize == 0); // Reopening must not publish bytes left by the previous event.
    auto const finalInfo = info;
    REQUIRE(mxlFlowReaderGetEventNonBlocking(reader, &info, &payload) == MXL_ERR_OUT_OF_RANGE_TOO_EARLY);
    REQUIRE(std::memcmp(&finalInfo, &info, sizeof info) == 0);
}

/** @test Recover each writer-crash stage both before the first entry and after wraparound. */
TEST_CASE("Event writer recovery preserves publication and timestamp order", "[events][recovery]")
{
    using namespace mxl::lib;
    using Ring = EventRingBuffer;
    auto const initialCount = GENERATE(std::uint64_t{0}, std::uint64_t{6});
    // 0: interrupted copy; 1: complete slot, stale head; 2: updated head, no notification.
    auto const crashStage = GENERATE(0, 1, 2);
    CAPTURE(initialCount, crashStage);

    struct Domain
    {
        std::filesystem::path path = mxl::tests::makeTempDomain();

        ~Domain()
        {
            auto error = std::error_code{};
            std::filesystem::remove_all(path, error);
        }
    };

    auto const domain = Domain{};
    auto manager = FlowManager{domain.path};
    auto watcher = std::make_shared<DomainWatcher>(domain.path);
    auto factory = PosixFlowIoFactory{watcher};
    auto const flowId = uuids::uuid::from_string(id).value();
    auto [created, data] = manager.createOrOpenEventFlow(flowId, definition, 4, mxlRational{20, 1}, 13);
    REQUIRE(created);
    auto* flow = data->flow();
    auto info = mxlEventInfo{};
    info.version = EVENT_HEADER_VERSION;
    info.size = sizeof info;
    info.eventSize = 13;
    info.complete = 1;
    auto bytes = std::array<std::uint8_t, 13>{};
    bytes.fill(0xAB);
    for (auto i = std::uint64_t{0}; i < initialCount; ++i)
    {
        info.timestamp = 10 + i;
        REQUIRE(data->ring().publish(i, info, bytes.data()) == MXL_STATUS_OK);
        flow->info.runtime.headIndex = i;
    }
    auto const eventFile = makeEventDataFilePath(makeFlowDirectoryName(domain.path, id));
    auto mapping = SharedMemorySegment{eventFile.c_str(), AccessMode::READ_WRITE, Ring::bufferSize(4, 13), LockMode::None};
    auto* slot = reinterpret_cast<Event*>(
        static_cast<std::uint8_t*>(mapping.data()) + sizeof(EventRingHeader) + ((initialCount % 4) * Ring::slotStride(13)));
    info.timestamp = 100;
    if (crashStage == 0)
    {
        slot->header.sequence = initialCount << 1;
        std::memset(slot + 1, 0xCD, 13); // Partial stale payload that recovery must replace completely.
    }
    else
    {
        REQUIRE(data->ring().publish(initialCount, info, bytes.data()) == MXL_STATUS_OK);
        if (crashStage == 2)
        {
            flow->info.runtime.headIndex = initialCount;
        }
    }
    auto const previous = flow->state.syncCounter;
    auto waiter = std::async(std::launch::async,
        [flow, previous] { return waitUntilChanged(&flow->state.syncCounter, previous, mxl::lib::Duration{1'000'000'000}); });
    auto writer = factory.createEventFlowWriter(manager, flowId, std::move(data));
    auto payload = static_cast<std::uint8_t*>(nullptr);
    REQUIRE(writer->openEvent(&info, &payload) == MXL_STATUS_OK);
    if (crashStage == 0)
    {
        REQUIRE(writer->getFlowRuntimeInfo().headIndex == (initialCount == 0 ? MXL_UNDEFINED_INDEX : initialCount - 1));
        info.timestamp = 100;
        info.eventSize = 13;
        std::memcpy(payload, bytes.data(), bytes.size());
        REQUIRE(writer->commit(info) == MXL_STATUS_OK);
    }
    else
    {
        REQUIRE(writer->getFlowRuntimeInfo().headIndex == initialCount);
        info.timestamp = 99;
        REQUIRE(writer->commit(info) == MXL_ERR_INVALID_ARG);
        REQUIRE(writer->cancel() == MXL_STATUS_OK);
    }
    REQUIRE(waiter.get());
    REQUIRE(flow->state.syncCounter != previous);
    auto ring = Ring{mapping.data(), 4, 13};
    auto received = mxlEventInfo{};
    auto copied = std::array<std::uint8_t, 13>{};
    REQUIRE(ring.read(initialCount, received, copied.data()) == Ring::ReadResult::Ready);
    REQUIRE(received.timestamp == 100);
    REQUIRE(received.index == initialCount);
    REQUIRE(copied == bytes);
    REQUIRE(ring.publish(initialCount, received, bytes.data()) == MXL_ERR_FLOW_INVALID);
    REQUIRE(writer->openEvent(&info, &payload) == MXL_STATUS_OK);
    info.timestamp = 100;
    REQUIRE(writer->commit(info) == MXL_STATUS_OK);
    REQUIRE(writer->getFlowRuntimeInfo().headIndex == initialCount + 1);
}
