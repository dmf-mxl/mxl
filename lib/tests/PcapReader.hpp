// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Minimal reader for the UDP datagrams in the pcap files used by the tests.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include "Utils.hpp"

namespace mxl::tests
{
    struct UdpDatagram
    {
        std::uint16_t dstPort;
        std::vector<std::uint8_t> payload;
    };

    std::vector<UdpDatagram> readUdpDatagrams(std::filesystem::path const& path);

    namespace detail
    {
        inline std::uint16_t readBigEndian16(std::span<std::uint8_t const> bytes, std::size_t offset)
        {
            return static_cast<std::uint16_t>((bytes[offset] << 8U) | bytes[offset + 1]);
        }

        inline std::uint32_t readPcap32(std::span<std::uint8_t const> bytes, std::size_t offset, bool bigEndian)
        {
            auto value = std::uint32_t{};
            for (auto i = std::size_t{}; i < 4; ++i)
            {
                auto const byte = bigEndian ? bytes[offset + i] : bytes[offset + 3 - i];
                value = (value << 8U) | byte;
            }
            return value;
        }
    }

    inline std::vector<UdpDatagram> readUdpDatagrams(std::filesystem::path const& path)
    {
        constexpr auto globalHeaderSize = std::size_t{24};
        constexpr auto recordHeaderSize = std::size_t{16};
        constexpr auto ethernetHeaderSize = std::size_t{14};
        constexpr auto vlanTagSize = std::size_t{4};
        constexpr auto udpHeaderSize = std::size_t{8};
        constexpr auto linkTypeEthernet = std::uint32_t{1};
        constexpr auto etherTypeVlan = std::uint16_t{0x8100};
        constexpr auto etherTypeIpv4 = std::uint16_t{0x0800};
        constexpr auto ipProtocolUdp = std::uint8_t{17};

        auto const contents = readFile(path);
        auto const file = std::span{reinterpret_cast<std::uint8_t const*>(contents.data()), contents.size()};
        auto const fail = [&path](char const* reason)
        {
            throw std::runtime_error{"Invalid pcap file " + path.string() + ": " + reason};
        };

        if (file.size() < globalHeaderSize)
        {
            fail("missing global header");
        }
        auto const magic = detail::readPcap32(file, 0, true);
        auto const bigEndian = (magic == 0xA1B2C3D4U) || (magic == 0xA1B23C4DU);
        // timestamps are ignored
        if (!bigEndian && (magic != 0xD4C3B2A1U) && (magic != 0x4D3CB2A1U))
        {
            fail("not a classic pcap file");
        }
        if (detail::readPcap32(file, 20, bigEndian) != linkTypeEthernet)
        {
            fail("link type is not Ethernet");
        }

        auto datagrams = std::vector<UdpDatagram>{};
        auto offset = globalHeaderSize;
        while (offset < file.size())
        {
            if (file.size() - offset < recordHeaderSize)
            {
                fail("truncated record header");
            }
            auto const capturedLength = detail::readPcap32(file, offset + 8, bigEndian);
            auto const originalLength = detail::readPcap32(file, offset + 12, bigEndian);
            offset += recordHeaderSize;
            if ((file.size() - offset < capturedLength) || (capturedLength != originalLength))
            {
                fail("truncated packet");
            }
            auto const frame = file.subspan(offset, capturedLength);
            offset += capturedLength;

            if (frame.size() < ethernetHeaderSize)
            {
                continue;
            }
            auto ipOffset = ethernetHeaderSize;
            auto etherType = detail::readBigEndian16(frame, 12);
            if ((etherType == etherTypeVlan) && (frame.size() >= ethernetHeaderSize + vlanTagSize))
            {
                etherType = detail::readBigEndian16(frame, 16);
                ipOffset += vlanTagSize;
            }
            if ((etherType != etherTypeIpv4) || (frame.size() < ipOffset + 20))
            {
                continue;
            }

            auto const ip = frame.subspan(ipOffset);
            auto const ipHeaderSize = std::size_t{ip[0] & 0x0FU} * 4U;
            auto const ipTotalLength = std::size_t{detail::readBigEndian16(ip, 2)};
            // skip IP fragments only the first one has a UDP header and tests do not need reassembly
            auto const fragmented = (detail::readBigEndian16(ip, 6) & 0x3FFFU) != 0;
            if (((ip[0] >> 4U) != 4) || (ip[9] != ipProtocolUdp) || fragmented || (ipHeaderSize < 20) || (ipTotalLength > ip.size()) ||
                (ipTotalLength < ipHeaderSize + udpHeaderSize))
            {
                continue;
            }

            auto const udp = ip.subspan(ipHeaderSize, ipTotalLength - ipHeaderSize);
            auto const udpLength = std::size_t{detail::readBigEndian16(udp, 4)};
            if ((udpLength < udpHeaderSize) || (udpLength > udp.size()))
            {
                fail("invalid UDP length");
            }
            auto const payload = udp.subspan(udpHeaderSize, udpLength - udpHeaderSize);
            datagrams.push_back(UdpDatagram{
                .dstPort = detail::readBigEndian16(udp, 2),
                .payload = {payload.begin(), payload.end()},
            });
        }
        return datagrams;
    }
}
