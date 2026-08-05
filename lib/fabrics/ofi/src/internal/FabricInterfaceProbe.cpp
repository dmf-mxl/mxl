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
        [[nodiscard]]
        std::optional<std::string> optStringFromCStr(char const* source)
        {
            return source == nullptr ? std::nullopt : std::make_optional<std::string>(source);
        }

        [[nodiscard]]
        std::string getHostname()
        {
            char buf[256] = {};
            ::gethostname(buf, sizeof(buf));
            return buf;
        }

        [[nodiscard]]
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

            try
            {
                auto const addressFormat = mustConvertAddressFormat(info->addr_format);
                auto addr = FabricAddress::decode(addressFormat, info->src_addr, info->src_addrlen);
                return addr.node() == requested.node();
            }
            catch (Exception const& ex)
            {
                if (ex.status() != MXL_ERR_INVALID_ARG)
                {
                    throw;
                }

                return false;
            }
        }
    }

    FabricInterfaceList probeInterfaces(std::optional<std::reference_wrapper<::mxlFabricsInterfaceConfig const>> query)
    {
        // Maps of provider specific configurations and addresses. They are lazily created when they appear on the fabric list.
        auto providerConfigs = std::map<Provider, ProviderConfig>{};
        auto fabricAddresses = std::map<Provider, std::optional<FabricAddress>>{};
        auto getProviderConfig = [&](Provider provider) -> ProviderConfig const&
        {
            auto it = providerConfigs.lower_bound(provider);
            if ((it == providerConfigs.end()) || (it->first != provider))
            {
                it = providerConfigs.emplace_hint(it, provider, ProviderConfig::create(provider, false, std::nullopt));
            }

            return it->second;
        };

        auto getFabricAddress = [&](Provider provider) -> std::optional<FabricAddress> const&
        {
            auto it = fabricAddresses.lower_bound(provider);
            if ((it == fabricAddresses.end()) || (it->first != provider))
            {
                try
                {
                    it = fabricAddresses.emplace_hint(it,
                        provider,
                        FabricAddress::parse(provider,
                            query ? optStringFromCStr(query.value().get().address.node) : std::nullopt,
                            query ? optStringFromCStr(query.value().get().address.service) : std::nullopt));
                }
                catch (Exception const&)
                {
                    it = fabricAddresses.emplace_hint(it, provider, std::nullopt);
                }
            }

            return it->second;
        };

        // clang-format off
        auto const requestedProvider = query 
            ? providerFromAPI(query.value().get().provider) 
            : Provider::ANY;
        // clang-format on
        if (!requestedProvider)
        {
            throw Exception::invalidArgument("Invalid provider");
        }

        auto list = FabricInterfaceList{};
        for (auto&& info : FabricInfoList::get())
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

        constexpr auto transferCapsMask = std::uint64_t{MXL_FABRICS_IFACE_CAP_REMOTE_WRITE | MXL_FABRICS_IFACE_CAP_SEND_RECEIVE};
        auto caps = ProviderCapabilities::fromAPI(interfaceConfig.caps);
        if ((caps.interfaceCaps & transferCapsMask) == 0)
        {
            throw Exception::invalidArgument(
                "Unsupported provider constraints: Missing transfer capability. Need either SEND_RECEIVE or REMOTE_WRITE");
        }
        else if ((caps.interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) == 0)
        {
            throw Exception::noFabric("Unsupported provider constraints: Only REMOTE_WRITE supported at this time.");
        }
        // RMA endpoints only bind a transmit CQ today. Drop SEND_RECEIVE so fi_getinfo does not
        // advertise FI_RECV (which would require a receive CQ at fi_connect / fi_listen).
        caps.interfaceCaps &= ~static_cast<std::uint64_t>(MXL_FABRICS_IFACE_CAP_SEND_RECEIVE);
        if (caps.maxMessageSize == 0)
        {
            MXL_WARN("maxMessageSize is not set. This field will be required in a future version.");
        }
        auto providerConfig = ProviderConfig::create(*provider, isTarget, caps);
        auto fabricAddress = FabricAddress::parse(
            *provider, optStringFromCStr(interfaceConfig.address.node), optStringFromCStr(interfaceConfig.address.service));

        auto sourceInterfaces = FabricInfoList::getSourceInterfaces(providerConfig, fabricAddress);
        auto first = sourceInterfaces.begin();
        auto const end = sourceInterfaces.end();
        if (first == end)
        {
            if ((caps.interfaceCaps & MXL_FABRICS_IFACE_CAP_HMEM) != 0)
            {
                throw Exception::noFabric(
                    "no HMEM-capable interface found for provider '{}'. Device grain payloads require a provider/domain with FI_HMEM "
                    "(e.g. verbs with nvidia_peermem loaded)",
                    providerConfig.getProviderName());
            }
            throw Exception::noFabric("no supported interfaces found");
        }

        // Prefer an info entry that passes provider filters (important for HMEM selection).
        for (auto info = first; info != end; ++info)
        {
            if (providerConfig.isSupportedFabricInfo(*info))
            {
                return {FabricInfo{*info}, std::move(providerConfig)};
            }
        }

        if ((caps.interfaceCaps & MXL_FABRICS_IFACE_CAP_HMEM) != 0)
        {
            throw Exception::noFabric(
                "no HMEM-capable interface found for provider '{}'. Device grain payloads require a provider/domain with FI_HMEM "
                "(e.g. verbs with nvidia_peermem loaded)",
                providerConfig.getProviderName());
        }

        // Host path: preserve historical behavior and accept the first fi_getinfo match.
        return {FabricInfo{*first}, std::move(providerConfig)};
    }
}
