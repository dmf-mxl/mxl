// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "TargetInfo.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <mxl-internal/JsonUtils.hpp>
#include <picojson/wrapper.h>
#include <uuid/uuid.h>
#include "Address.hpp"
#include "Exception.hpp"
#include "FabricAddress.hpp"
#include "Format.hpp" // IWYU pragma: keep; for fmt::format
#include "Provider.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    TargetInfo* TargetInfo::fromAPI(mxlFabricsTargetInfo api) noexcept
    {
        return reinterpret_cast<TargetInfo*>(api);
    }

    ::mxlFabricsTargetInfo TargetInfo::toAPI() noexcept
    {
        return reinterpret_cast<mxlFabricsTargetInfo>(this);
    }

    std::string TargetInfo::toJSON() const
    {
        auto root = picojson::object{};

        root["fabricAddress"] = picojson::value(fabricAddress.toBase64());
        root["addressFormat"] = picojson::value{static_cast<double>(fabricAddress.format())};

        auto regions = picojson::array{};
        for (auto const& remoteRegion : remoteRegions)
        {
            auto region = picojson::object{};
            region["addr"] = picojson::value(std::to_string(remoteRegion.addr));
            region["len"] = picojson::value(std::to_string(remoteRegion.len));
            region["rkey"] = picojson::value(std::to_string(remoteRegion.rkey));
            regions.emplace_back(region);
        }
        root["regions"] = picojson::value(regions);

        if (bounceBufferInfo)
        {
            auto bounceBufferInfoObj = picojson::object{};
            bounceBufferInfoObj["entryCount"] = picojson::value(std::to_string(bounceBufferInfo->entryCount));
            bounceBufferInfoObj["entrySize"] = picojson::value(std::to_string(bounceBufferInfo->entrySize));
            root["bounceBufferInfo"] = picojson::value(bounceBufferInfoObj);
        }

        root["id"] = picojson::value(std::to_string(id));
        root["provider"] = picojson::value{fmt::to_string(provider)};

        return picojson::value(root).serialize(false);
    }

    TargetInfo TargetInfo::fromJSON(std::string const& s)
    {
        // The target info has one region per grain, so it can be much larger than a flow definition.
        constexpr auto limits = JsonLimits{.maxInputSize = std::size_t{4} * 1024U * 1024U, .maxDepth = 8U, .maxContainerSize = 65536U};

        auto parsed = picojson::value{};
        auto const err = parseJson(parsed, s, limits);
        if (!err.empty())
        {
            throw Exception::invalidArgument("Failure when parsing JSON: {} ", err);
        }

        if (!parsed.is<picojson::object>())
        {
            throw Exception::invalidArgument("Expected a JSON object");
        }

        auto root = parsed.get<picojson::object>();

        // The conversion helpers throw std::invalid_argument, which the C API would report as MXL_ERR_UNKNOWN.
        try
        {
            auto const addressFormat = mustConvertAddressFormat(jsonToInteger<std::uint32_t>(root.at("addressFormat"), "addressFormat"));
            auto const fabricAddress = RawFabricAddress::fromBase64(root.at("fabricAddress").get<std::string>(), addressFormat);

            auto regions = std::vector<RemoteRegion>{};
            for (auto const& regionValue : root.at("regions").get<picojson::array>())
            {
                auto const regionObj = regionValue.get<picojson::object>();

                // Conversions of types that represent memory addresses to strings is preferred over converting to float
                // to make sure nobody can reduce precision when converting on the way.
                auto const addr = jsonStringToInteger<std::uint64_t>(regionObj.at("addr"), "addr");
                auto const len = jsonStringToInteger<std::size_t>(regionObj.at("len"), "len");
                auto const rkey = jsonStringToInteger<std::uint64_t>(regionObj.at("rkey"), "rkey");
                regions.emplace_back(addr, len, rkey);
            }

            auto bounceBufferInfo = std::optional<TargetInfoBounceBufferInfo>{std::nullopt};
            if (root.contains("bounceBufferInfo"))
            {
                auto const bounceBufferInfoObj = root.at("bounceBufferInfo").get<picojson::object>();
                auto const entryCount = jsonStringToInteger<std::size_t>(bounceBufferInfoObj.at("entryCount"), "entryCount");
                auto const entrySize = jsonStringToInteger<std::size_t>(bounceBufferInfoObj.at("entrySize"), "entrySize");
                bounceBufferInfo = std::make_optional<TargetInfoBounceBufferInfo>(entryCount, entrySize);
            }

            auto const id = jsonStringToInteger<Endpoint::Id>(root.at("id"), "id");
            auto const provider = providerFromString(root.at("provider").get<std::string>()).value_or(Provider::ANY);
            return {.id = id, .fabricAddress = fabricAddress, .provider = provider, .remoteRegions = regions, .bounceBufferInfo = bounceBufferInfo};
        }
        catch (std::invalid_argument const& e)
        {
            throw Exception::invalidArgument("{}", e.what());
        }
    }

    bool TargetInfo::operator==(TargetInfo const& other) const noexcept
    {
        return (fabricAddress == other.fabricAddress) && (remoteRegions == other.remoteRegions) && (id == other.id);
    }
}
