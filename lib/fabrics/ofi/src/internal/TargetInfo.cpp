// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "TargetInfo.hpp"
#include <string>
#include <picojson/picojson.h>
#include <uuid/uuid.h>
#include "Address.hpp"
#include "Exception.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    TargetInfo* TargetInfo::fromAPI(mxlTargetInfo api) noexcept
    {
        return reinterpret_cast<TargetInfo*>(api);
    }

    ::mxlTargetInfo TargetInfo::toAPI() noexcept
    {
        return reinterpret_cast<mxlTargetInfo>(this);
    }

    std::string TargetInfo::toJSON() const
    {
        auto root = picojson::object{};

        root["fabricAddress"] = picojson::value(fabricAddress.toBase64());

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
        root["id"] = picojson::value(std::to_string(id));

        return picojson::value(root).serialize(false);
    }

    TargetInfo TargetInfo::fromJSON(std::string const& s)
    {
        auto parsed = picojson::value{};
        auto const err = picojson::parse(parsed, s);
        if (!err.empty())
        {
            throw Exception::invalidArgument("Failure when parsing JSON: {} ", err);
        }

        if (!parsed.is<picojson::object>())
        {
            throw Exception::invalidArgument("Expected a JSON object");
        }

        auto root = parsed.get<picojson::object>();

        auto fabricAddress = FabricAddress::fromBase64(root.at("fabricAddress").get<std::string>());

        auto regions = std::vector<RemoteRegion>{};
        auto regionsArray = root.at("regions").get<picojson::array>();
        for (auto const& regionValue : regionsArray)
        {
            auto regionObj = regionValue.get<picojson::object>();
            auto addr = std::stoull(regionObj.at("addr").get<std::string>());
            auto len = std::stoull(regionObj.at("len").get<std::string>());
            auto rkey = std::stoull(regionObj.at("rkey").get<std::string>());
            regions.emplace_back(addr, len, rkey);
        }

        auto id = std::stoull(root.at("id").get<std::string>());

        return {.fabricAddress = fabricAddress, .remoteRegions = regions, .id = id};
    }

    bool TargetInfo::operator==(TargetInfo const& other) const noexcept
    {
        return fabricAddress == other.fabricAddress && remoteRegions == other.remoteRegions && id == other.id;
    }
}
