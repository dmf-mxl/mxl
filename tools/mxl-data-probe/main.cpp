// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <ada.h>
#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <picojson/picojson.h>
#include <mxl/flow.h>
#include <mxl/flowinfo.h>
#include <mxl/mxl.h>

namespace
{
    constexpr auto const Rfc8331HeaderSizeBytes = std::size_t{6U};
    constexpr auto const Lower8Bits = std::uint16_t{0x00ffU};
    constexpr auto const Lower10Bits = std::uint16_t{0x03ffU};
    constexpr auto const Lower11Bits = std::uint16_t{0x07ffU};

    [[nodiscard]]
    constexpr char const* statusToString(::mxlStatus status) noexcept
    {
        switch (status)
        {
            case MXL_STATUS_OK:                  return "MXL_STATUS_OK";
            case MXL_ERR_UNKNOWN:                return "MXL_ERR_UNKNOWN";
            case MXL_ERR_FLOW_NOT_FOUND:         return "MXL_ERR_FLOW_NOT_FOUND";
            case MXL_ERR_OUT_OF_RANGE_TOO_LATE:  return "MXL_ERR_OUT_OF_RANGE_TOO_LATE";
            case MXL_ERR_OUT_OF_RANGE_TOO_EARLY: return "MXL_ERR_OUT_OF_RANGE_TOO_EARLY";
            case MXL_ERR_INVALID_FLOW_READER:    return "MXL_ERR_INVALID_FLOW_READER";
            case MXL_ERR_INVALID_FLOW_WRITER:    return "MXL_ERR_INVALID_FLOW_WRITER";
            case MXL_ERR_TIMEOUT:                return "MXL_ERR_TIMEOUT";
            case MXL_ERR_INVALID_ARG:            return "MXL_ERR_INVALID_ARG";
            case MXL_ERR_CONFLICT:               return "MXL_ERR_CONFLICT";
            case MXL_ERR_PERMISSION_DENIED:      return "MXL_ERR_PERMISSION_DENIED";
            case MXL_ERR_FLOW_INVALID:           return "MXL_ERR_FLOW_INVALID";
            case MXL_ERR_STRLEN:                 return "MXL_ERR_STRLEN";
            case MXL_ERR_INTERRUPTED:            return "MXL_ERR_INTERRUPTED";
            case MXL_ERR_NO_FABRIC:              return "MXL_ERR_NO_FABRIC";
            case MXL_ERR_INVALID_STATE:          return "MXL_ERR_INVALID_STATE";
            case MXL_ERR_INTERNAL:               return "MXL_ERR_INTERNAL";
            case MXL_ERR_NOT_READY:              return "MXL_ERR_NOT_READY";
            case MXL_ERR_NOT_FOUND:              return "MXL_ERR_NOT_FOUND";
            case MXL_ERR_EXISTS:                 return "MXL_ERR_EXISTS";
            default:                             return "MXL_ERR_UNRECOGNIZED";
        }
    }

    class ScopedMxlInstance
    {
    public:
        explicit ScopedMxlInstance(std::string const& domain)
            : _instance{::mxlCreateInstance(domain.c_str(), "")}
        {
            if (_instance == nullptr)
            {
                throw std::runtime_error{"Failed to create MXL instance."};
            }
        }

        ~ScopedMxlInstance()
        {
            (void)::mxlDestroyInstance(_instance);
        }

        ScopedMxlInstance(ScopedMxlInstance const&) = delete;
        ScopedMxlInstance& operator=(ScopedMxlInstance const&) = delete;

        [[nodiscard]]
        constexpr ::mxlInstance get() const noexcept
        {
            return _instance;
        }

    private:
        ::mxlInstance _instance;
    };

    class ScopedFlowReader
    {
    public:
        ScopedFlowReader(::mxlInstance instance, std::string const& flowId)
            : _instance{instance}
            , _reader{nullptr}
        {
            auto const status = ::mxlCreateFlowReader(instance, flowId.c_str(), "", &_reader);
            if (status != MXL_STATUS_OK)
            {
                throw std::runtime_error{"Failed to create flow reader: " + std::string{statusToString(status)}};
            }
        }

        ~ScopedFlowReader()
        {
            (void)::mxlReleaseFlowReader(_instance, _reader);
        }

        ScopedFlowReader(ScopedFlowReader const&) = delete;
        ScopedFlowReader& operator=(ScopedFlowReader const&) = delete;

        [[nodiscard]]
        ::mxlFlowReader get() const noexcept
        {
            return _reader;
        }

    private:
        ::mxlInstance _instance;
        ::mxlFlowReader _reader;
    };

    class BigEndianWordReader
    {
    public:
        constexpr explicit BigEndianWordReader(std::span<std::uint8_t const> bytes)
            : _bytes{bytes}
            , _offset{0}
        {}

        [[nodiscard]]
        constexpr std::uint16_t read()
        {
            if ((_bytes.size() - _offset) < sizeof(std::uint16_t))
            {
                throw std::runtime_error{"Out-of-bounds read while parsing RFC-8331 ANC payload."};
            }

            auto const value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(_bytes[_offset]) << 8U) | _bytes[_offset + 1]);
            _offset += sizeof(std::uint16_t);
            return value;
        }

        [[nodiscard]]
        constexpr std::size_t wordOffset() const noexcept
        {
            return _offset / sizeof(std::uint16_t);
        }

        [[nodiscard]]
        constexpr std::size_t byteOffset() const noexcept
        {
            return _offset;
        }

    private:
        std::span<std::uint8_t const> _bytes;
        std::size_t _offset;
    };

    class UdwUnpacker
    {
    public:
        constexpr explicit UdwUnpacker(BigEndianWordReader& reader)
            : _reader{&reader}
            , _accumulatedBits{0}
            , _bitCount{0}
        {}

        [[nodiscard]]
        constexpr std::uint16_t read()
        {
            while (_bitCount < 10U)
            {
                _accumulatedBits = (_accumulatedBits << 16U) | _reader->read();
                _bitCount += 16U;
            }

            auto const value = static_cast<std::uint16_t>((_accumulatedBits >> (_bitCount - 10U)) & Lower10Bits);
            _bitCount -= 10U;
            _accumulatedBits &= (1U << _bitCount) - 1U;
            return value;
        }

    private:
        BigEndianWordReader* _reader;
        std::uint32_t _accumulatedBits;
        std::uint32_t _bitCount;
    };

    struct AncElement
    {
        std::uint16_t lineNumber = 0;
        std::uint8_t did = 0;
        std::uint8_t sdid = 0;
        std::uint8_t dataCount = 0;
        std::vector<std::uint8_t> udw;
    };

    struct AncFrame
    {
        std::uint16_t length = 0;
        std::uint8_t ancCount = 0;
        std::vector<AncElement> elements;
    };

    AncFrame parseAncFrame(std::span<std::uint8_t const> payload)
    {
        if (payload.size() < Rfc8331HeaderSizeBytes)
        {
            throw std::runtime_error{"Grain payload is too small to contain an RFC-8331 ANC header."};
        }

        auto reader = BigEndianWordReader{payload};

        auto frame = AncFrame{};
        frame.length = reader.read();
        auto const word1 = reader.read();
        frame.ancCount = static_cast<std::uint8_t>(word1 >> 8U);
        (void)reader.read();

        auto const packetSize = static_cast<std::size_t>(frame.length) + Rfc8331HeaderSizeBytes;
        if (packetSize > payload.size())
        {
            throw std::runtime_error{"RFC-8331 Length field exceeds the available grain payload."};
        }

        for (auto i = std::uint8_t{0}; i < frame.ancCount; ++i)
        {
            if ((reader.wordOffset() % 2U) == 0U)
            {
                (void)reader.read();
            }

            auto element = AncElement{};
            auto const word0 = reader.read();
            (void)reader.read();
            auto const word2 = reader.read();
            auto const word3 = reader.read();

            element.lineNumber = static_cast<std::uint16_t>((word0 >> 4U) & Lower11Bits);
            element.did = static_cast<std::uint8_t>((word2 >> 6U) & Lower8Bits);
            element.sdid = static_cast<std::uint8_t>(((word2 & 0x000fU) << 4U) | ((word3 >> 12U) & 0x000fU));
            element.dataCount = static_cast<std::uint8_t>((word3 >> 2U) & Lower8Bits);
            element.udw.reserve(element.dataCount);

            auto unpacker = UdwUnpacker{reader};
            for (auto word = std::uint8_t{0}; word < element.dataCount; ++word)
            {
                element.udw.push_back(static_cast<std::uint8_t>(unpacker.read() & Lower8Bits));
            }

            (void)unpacker.read();
            frame.elements.push_back(std::move(element));
        }

        return frame;
    }

    std::string formatHexByte(std::uint8_t value)
    {
        return fmt::format("0x{:02X}", value);
    }

    struct AncDataType
    {
        std::uint8_t did;
        std::uint8_t sdid;
        char const* description;
    };

    constexpr AncDataType CommonAncDataTypes[] = {
        {.did = 0x41U, .sdid = 0x01U, .description = "SMPTE ST 352 Video Payload ID"           },
        {.did = 0x41U, .sdid = 0x05U, .description = "SMPTE ST 2016 Active Format Description" },
        {.did = 0x41U, .sdid = 0x07U, .description = "ANSI/SCTE 104 messages"                  },
        {.did = 0x41U, .sdid = 0x08U, .description = "SMPTE ST 2031 VBI data"                  },
        {.did = 0x41U, .sdid = 0x0CU, .description = "SMPTE ST 2108-1 HDR/WCG metadata"        },
        {.did = 0x43U, .sdid = 0x02U, .description = "OP-47 Subtitling Distribution Packet"    },
        {.did = 0x43U, .sdid = 0x03U, .description = "OP-47 VANC multipacket"                  },
        {.did = 0x50U, .sdid = 0x01U, .description = "Wide Screen Signaling"                   },
        {.did = 0x60U, .sdid = 0x60U, .description = "SMPTE ST 12-2 Ancillary Time Code"       },
        {.did = 0x61U, .sdid = 0x01U, .description = "SMPTE ST 334 Caption Distribution Packet"},
        {.did = 0x61U, .sdid = 0x02U, .description = "SMPTE ST 334 CEA-608 closed captions"    },
    };

    [[nodiscard]]
    constexpr char const* describeAncDataType(std::uint8_t did, std::uint8_t sdid) noexcept
    {
        for (auto const& dataType : CommonAncDataTypes)
        {
            if ((dataType.did == did) && (dataType.sdid == sdid))
            {
                return dataType.description;
            }
        }
        return "Unknown ancillary data";
    }

    void printAncFrame(std::uint64_t index, ::mxlGrainInfo const& grainInfo, AncFrame const& frame)
    {
        fmt::print("Grain {}\n", index);
        fmt::print("  flags: 0x{:X}\n", grainInfo.flags);
        fmt::print("  grain size: {} bytes\n", grainInfo.grainSize);
        fmt::print("  valid slices: {}/{}\n", grainInfo.validSlices, grainInfo.totalSlices);
        fmt::print("  RFC-8331 length: {} bytes\n", frame.length);
        fmt::print("  ANC count: {}\n", frame.ancCount);

        if (frame.elements.empty())
        {
            fmt::print("  No ANC elements\n");
            return;
        }

        for (auto i = std::size_t{0}; i < frame.elements.size(); ++i)
        {
            auto const& element = frame.elements[i];
            fmt::print("  ANC element {}\n", i);
            fmt::print("    line: {}\n", element.lineNumber);
            fmt::print("    DID/SDID: {}/{} - {}\n",
                formatHexByte(element.did),
                formatHexByte(element.sdid),
                describeAncDataType(element.did, element.sdid));
            fmt::print("    DC: {}\n", element.dataCount);
            fmt::print("    UDW:");
            if (element.udw.empty())
            {
                fmt::print(" <empty>");
            }
            else
            {
                for (auto const value : element.udw)
                {
                    fmt::print(" {}", formatHexByte(value));
                }
            }
            fmt::print("\n");
        }
    }

    std::size_t readablePayloadSize(::mxlGrainInfo const& grainInfo)
    {
        auto size = static_cast<std::size_t>(grainInfo.grainSize);
        if ((grainInfo.validSlices > 0U) && (grainInfo.validSlices < grainInfo.totalSlices))
        {
            size = std::min(size, static_cast<std::size_t>(grainInfo.validSlices));
        }
        return size;
    }

    std::string getMediaType(::mxlInstance instance, std::string const& flowId)
    {
        auto buffer = std::vector<char>(4096);
        auto bufferSize = buffer.size();
        auto status = ::mxlGetFlowDef(instance, flowId.c_str(), buffer.data(), &bufferSize);

        if ((status == MXL_ERR_INVALID_ARG) && (bufferSize > buffer.size()))
        {
            buffer.resize(bufferSize);
            status = ::mxlGetFlowDef(instance, flowId.c_str(), buffer.data(), &bufferSize);
        }

        if (status != MXL_STATUS_OK)
        {
            throw std::runtime_error{"Failed to get flow definition: " + std::string{statusToString(status)}};
        }

        auto json = picojson::value{};
        auto const error = picojson::parse(json, buffer.data());
        if (!error.empty())
        {
            throw std::runtime_error{"Failed to parse flow definition JSON: " + error};
        }

        if (!json.is<picojson::object>())
        {
            throw std::runtime_error{"Flow definition is not a JSON object."};
        }

        auto const& object = json.get<picojson::object>();
        auto const iter = object.find("media_type");
        if ((iter == object.end()) || !iter->second.is<std::string>())
        {
            return {};
        }
        return iter->second.get<std::string>();
    }

    void readDataFlow(std::string const& domain, std::string const& flowId, std::uint64_t count, std::uint64_t timeoutNs)
    {
        auto instance = ScopedMxlInstance{domain};
        auto reader = ScopedFlowReader{instance.get(), flowId};

        auto flowInfo = ::mxlFlowInfo{};
        auto status = ::mxlFlowReaderGetInfo(reader.get(), &flowInfo);
        if (status != MXL_STATUS_OK)
        {
            throw std::runtime_error{fmt::format("Failed to get flow info: {}", statusToString(status))};
        }

        if (flowInfo.config.common.format != MXL_DATA_FORMAT_DATA)
        {
            throw std::runtime_error{"Flow is not a Data flow."};
        }

        auto const mediaType = getMediaType(instance.get(), flowId);
        if (mediaType != "video/smpte291")
        {
            auto message = std::string{"Flow is not an ancillary Data flow. Expected media_type video/smpte291"};
            if (!mediaType.empty())
            {
                message += fmt::format(", got {}", mediaType);
            }
            throw std::runtime_error{message + "."};
        }

        auto index = flowInfo.runtime.headIndex;
        fmt::print("Reading Data flow {} from head index {}\n", flowId, index);

        for (std::uint64_t readCount = 0; readCount < count; ++readCount, ++index)
        {
            auto grainInfo = ::mxlGrainInfo{};
            auto* payload = static_cast<std::uint8_t*>(nullptr);
            status = ::mxlFlowReaderGetGrain(reader.get(), index, timeoutNs, &grainInfo, &payload);
            if (status != MXL_STATUS_OK)
            {
                throw std::runtime_error{fmt::format("Failed to read grain {}: {}", index, statusToString(status))};
            }

            try
            {
                auto const payloadSize = readablePayloadSize(grainInfo);
                auto const frame = parseAncFrame(std::span<std::uint8_t const>{payload, payloadSize});
                printAncFrame(index, grainInfo, frame);
            }
            catch (std::exception const& ex)
            {
                throw std::runtime_error{fmt::format("Failed to parse grain {}: {}", index, ex.what())};
            }
        }
    }
}

int main(int argc, char** argv)
{
    auto app = CLI::App{"Read RFC-8331 ANC elements from an MXL Data flow."};
    app.footer("MXL URI format:\n"
               "    mxl://[authority[:port]]/domain[?id=...]\n"
               "    See: https://github.com/dmf-mxl/mxl/docs/Addressability.md");

    auto version = ::mxlVersionType{};
    ::mxlGetVersion(&version);
    app.set_version_flag("--version", version.full);

    auto domain = std::string{};
    auto* domainOpt = app.add_option("-d,--domain", domain, "The MXL domain directory");
    domainOpt->check(CLI::ExistingDirectory);

    auto flowId = std::string{};
    app.add_option("-f,--flow", flowId, "The flow id to read");

    auto count = std::uint64_t{1};
    app.add_option("-c,--count", count, "Number of grains to read from the current head index")
        ->check(CLI::Range(std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()));

    auto timeoutMs = std::uint64_t{1000};
    app.add_option("-t,--timeout-ms", timeoutMs, "Timeout per grain read in milliseconds");

    auto address = std::vector<std::string>{};
    app.add_option("ADDRESS", address, "MXL URI")->expected(-1);

    CLI11_PARSE(app, argc, argv);

    // URI will overwrite any other redundant options. Parse the URI after CLI11 parsing.
    if (!address.empty())
    {
        auto const parsedUri = ada::parse<ada::url_aggregator>(address.at(0));
        if (!parsedUri)
        {
            fmt::print(stderr, "ERROR: Invalid MXL URI.\n");
            return EXIT_FAILURE;
        }

        auto const path = parsedUri->get_pathname();
        if (path.empty())
        {
            fmt::print(stderr, "ERROR: Domain must be specified in the MXL URI.\n");
            return EXIT_FAILURE;
        }

        if (!parsedUri->get_hostname().empty() || !parsedUri->get_port().empty())
        {
            fmt::print(stderr, "ERROR: Authority/port not currently supported in MXL URI.\n");
            return EXIT_FAILURE;
        }

        domain = std::string{path};

        auto query = ada::url_search_params{parsedUri->get_search()};
        if (auto const id = query.get("id"))
        {
            flowId = std::string{*id};
        }
    }

    if (domain.empty())
    {
        fmt::print(stderr, "ERROR: Domain must be specified either via --domain or in the URI.\n");
        return EXIT_FAILURE;
    }

    if (flowId.empty())
    {
        fmt::print(stderr, "ERROR: Flow id must be specified either via --flow or in the URI id query parameter.\n");
        return EXIT_FAILURE;
    }

    try
    {
        readDataFlow(domain, flowId, count, timeoutMs * 1000U * 1000U);
        return EXIT_SUCCESS;
    }
    catch (std::exception const& ex)
    {
        fmt::print(stderr, "ERROR: {}\n", ex.what());
        return EXIT_FAILURE;
    }
}
