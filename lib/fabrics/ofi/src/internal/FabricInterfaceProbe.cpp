// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#include "FabricInterfaceProbe.hpp"
#include <functional>
#include <optional>
#include <unistd.h>
#include <rdma/fabric.h>
#include "mxl-internal/Logging.hpp"
#include "Exception.hpp"
#include "FabricAddress.hpp"
#include "FabricInfo.hpp"
#include "FabricInterfaceList.hpp"
#include "Format.hpp" // IWYU pragma: keep; used in fmt::to_string()
#include "Provider.hpp"
#include "ProviderConfig.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        std::optional<std::string> optStringFromCStr(char const* source)
        {
            return source == nullptr ? std::nullopt : std::make_optional<std::string>(source);
        }

        std::string getHostname()
        {
            char buf[256] = {};
            ::gethostname(buf, sizeof(buf));
            return buf;
        }

        bool matchesRequestedAddress(FabricAddress const& requested, FabricInfoView info)
        {
            auto provider = providerFromString(info->fabric_attr->prov_name);
            if (!provider)
            {
                return false;
            }

            if (requested.empty())
            {
                return true;
            }

            if (*provider == Provider::SHM)
            {
                return requested.node() == getHostname();
            }

            auto const addressFormat = mustConvertAddressFormat(info->addr_format);
            auto addr = FabricAddress::decode(addressFormat, info->src_addr, info->src_addrlen);
            return addr.node() == requested.node();
        }
    }

    FabricInterfaceList probeInterfaces(std::optional<std::reference_wrapper<::mxlFabricsInterfaceConfig const>> query)
    {
        // Maps of provider specific configurations and addresses. They are lazily created when they appear on the fabric list.
        auto providerConfigs = std::map<Provider, ProviderConfig>{};
        auto fabricAddresses = std::map<Provider, std::optional<FabricAddress>>{};
        auto getProviderConfig = [&](Provider provider) -> ProviderConfig const&
        {
            auto found = providerConfigs.find(provider);
            if (found != providerConfigs.end())
            {
                return found->second;
            }

            auto [emplaced, _] = providerConfigs.emplace(provider, ProviderConfig::create(provider, false, std::nullopt));
            return emplaced->second;
        };

        auto getFabricAddress = [&](Provider provider) -> std::optional<FabricAddress> const&
        {
            auto found = fabricAddresses.find(provider);
            if (found != fabricAddresses.end())
            {
                return found->second;
            }

            try
            {
                auto [emplaced, _] = fabricAddresses.emplace(provider,
                    FabricAddress::parse(provider,
                        query ? optStringFromCStr(query.value().get().address.node) : std::nullopt,
                        query ? optStringFromCStr(query.value().get().address.service) : std::nullopt));

                return emplaced->second;
            }
            catch (Exception const&)
            {
                return fabricAddresses.emplace(provider, std::nullopt).first->second;
            }
        };

        auto const requestedProvider = query ? providerFromAPI(query.value().get().provider) : Provider::ANY;
        if (!requestedProvider)
        {
            throw Exception::invalidArgument("Invalid provider");
        }

        auto list = FabricInterfaceList{};
        for (auto info : FabricInfoList::get())
        {
            // Ignore if the provider of this fabric info is not known.
            auto provider = providerFromString(info->fabric_attr->prov_name);
            if (!provider)
            {
                continue;
            }

            // Ignore if a specific provider is requested, and this one does not match.
            if ((requestedProvider != Provider::ANY) && (provider != requestedProvider))
            {
                continue;
            }

            // Ignore if the query address could not be parsed for this provider
            auto const& address = getFabricAddress(*provider);
            if (!address)
            {
                continue;
            }

            // Ignore if the info does not match the requested address
            if (!matchesRequestedAddress(*address, info))
            {
                continue;
            }

            // Ignore if no supported.
            auto const& config = getProviderConfig(*provider);
            if (config.isSupportedFabricInfo(info))
            {
                auto description = FabricInterfaceDescription::create(info);
                if (description)
                {
                    list.push(*description);
                }
            }
        }

        return list;
    }

    std::pair<FabricInfo, ProviderConfig> selectSourceInterface(::mxlFabricsInterfaceConfig const& interfaceConfig, bool isTarget)
    {
        auto provider = providerFromAPI(interfaceConfig.provider);
        if (!provider)
        {
            throw Exception::invalidArgument("invalid provider");
        }

        constexpr auto transferCaps = std::uint64_t{MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_SEND_RECEIVE};
        auto caps = ProviderCapabilities::fromAPI(interfaceConfig.caps);
        if ((caps.interfaceCaps & transferCaps) == 0)
        {
            MXL_WARN("No transfer capabilities requested, defaulting to REMOTE_WRITE");
            caps.interfaceCaps |= MXL_FABRICS_IFACE_CAP_REMOTE_WRITE;
        }
        else if ((caps.interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) == 0)
        {
            throw Exception::noFabric("Unsupported provider constraints. Only REMOTE_WRITE supported at this time.");
        }
        if (caps.maxMessageSize == 0)
        {
            MXL_WARN("maxMessageSize is not set. This field will be required in a future version.");
        }
        auto providerConfig = ProviderConfig::create(*provider, isTarget, caps);
        auto fabricAddress = FabricAddress::parse(
            *provider, optStringFromCStr(interfaceConfig.address.node), optStringFromCStr(interfaceConfig.address.service));

        auto sourceInterfaces = FabricInfoList::getSourceInterfaces(providerConfig, fabricAddress);
        if (std::ranges::distance(sourceInterfaces) == 0)
        {
            throw Exception::noFabric("no supported interfaces found");
        }

        return {*sourceInterfaces.begin(), std::move(providerConfig)};
    }
}
