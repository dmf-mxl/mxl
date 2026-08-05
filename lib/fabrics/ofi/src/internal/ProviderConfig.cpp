// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include "ProviderConfig.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        namespace
        {
            constexpr auto const supportedMemoryRegistrationModes = std::uint64_t{FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY};
        }

        [[nodiscard]]
        bool requiresHmem(std::optional<ProviderCapabilities> const& capabilities) noexcept
        {
            return capabilities && ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_HMEM) != 0);
        }

        std::uint64_t libfabricCaps(std::optional<ProviderCapabilities> const& capabilities, bool isTarget)
        {
            if (!capabilities)
            {
                return 0;
            }
            auto result = std::uint64_t{0};
            if ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) != 0)
            {
                result |= (isTarget ? FI_REMOTE_WRITE : FI_WRITE) | FI_RMA;
            }
            if ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_SEND_RECEIVE) != 0)
            {
                result |= FI_SEND | FI_RECV;
            }
            if ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_HMEM) != 0)
            {
                result |= FI_HMEM;
            }
            return result;
        }

        std::uint64_t libfabricRequiredCaps(std::optional<ProviderCapabilities> const& capabilities)
        {
            if (!capabilities)
            {
                return 0;
            }
            auto result = std::uint64_t{0};
            if ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) != 0)
            {
                result |= FI_RMA;
            }
            if ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_HMEM) != 0)
            {
                result |= FI_HMEM;
            }
            return result;
        }

        std::uint64_t memoryRegistrationModes(std::optional<ProviderCapabilities> const& capabilities) noexcept
        {
            auto modes = supportedMemoryRegistrationModes;
            if (requiresHmem(capabilities))
            {
                modes |= FI_MR_HMEM;
            }
            return modes;
        }

        /**
         * Host-only setups reject FI_HMEM domains so we keep the historical non-HMEM path.
         * When HMEM is required (device grain payloads), keep FI_HMEM domains and require the cap.
         * On the query path (no capabilities), do not filter FI_HMEM so callers can discover
         * HMEM-capable interfaces via mxlFabricsGetInterfaces().
         */
        std::uint64_t filteredCapsForProvider(std::uint64_t baseFilteredCaps, std::optional<ProviderCapabilities> const& capabilities) noexcept
        {
            if (!capabilities)
            {
                return baseFilteredCaps;
            }
            if (requiresHmem(capabilities))
            {
                return baseFilteredCaps & ~static_cast<std::uint64_t>(FI_HMEM);
            }
            return baseFilteredCaps | FI_HMEM;
        }
    }

    ProviderCapabilities ProviderCapabilities::fromAPI(::mxlFabricsInterfaceCaps caps)
    {
        return ProviderCapabilities{
            .maxMessageSize = caps.maxMessageSize,
            .interfaceCaps = caps.flags,
        };
    }

    ProviderConfig ProviderConfig::create(Provider provider, bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        switch (provider)
        {
            case Provider::TCP:   return ProviderConfig::tcp(isTarget, capabilities);
            case Provider::VERBS: return ProviderConfig::verbs(isTarget, capabilities);
            case Provider::EFA:   return ProviderConfig::efa(isTarget, capabilities);
            case Provider::SHM:   return ProviderConfig::shm(isTarget, capabilities);
            default:              throw Exception::invalidState("cannot create provider config for ANY provider");
        }
    }

    ProviderConfig ProviderConfig::tcp(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        // TCP has no real HMEM path today; requesting HMEM will fail at fi_getinfo / requiredCaps.
        // When concrete capabilities are supplied, include FI_MSG in hints so returned fi_info
        // objects advertise FI_MSG (requiredCaps checks it). Leave discovery (nullopt) unchanged.
        auto const msgCap = capabilities ? static_cast<std::uint64_t>(FI_MSG) : 0;
        auto values = ProviderConfigValues{
            .providerName = "tcp",
            .memoryRegistrationModes = static_cast<int>(memoryRegistrationModes(capabilities)),
            .endpointType = FI_EP_MSG,
            .caps = libfabricCaps(capabilities, isTarget) | msgCap,
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_SOCK_TCP},
            .requiredCaps = libfabricRequiredCaps(capabilities) | msgCap,
            .filteredCaps = filteredCapsForProvider(FI_TAGGED, capabilities),
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::verbs(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        // When concrete capabilities are supplied, include FI_MSG in hints so returned fi_info
        // objects advertise FI_MSG (requiredCaps checks it). Without this, HMEM-capable domains
        // can be returned without FI_MSG set and then rejected by isSupportedFabricInfo.
        // Discovery (nullopt) keeps historical zero-cap hints.
        auto const msgCap = capabilities ? static_cast<std::uint64_t>(FI_MSG) : 0;
        auto values = ProviderConfigValues{
            .providerName = "verbs",
            .memoryRegistrationModes = static_cast<int>(memoryRegistrationModes(capabilities)),
            .endpointType = FI_EP_MSG,
            .caps = libfabricCaps(capabilities, isTarget) | msgCap,
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_RDMA_CM_IB_RC},
            .requiredCaps = libfabricRequiredCaps(capabilities) | msgCap,
            .filteredCaps = filteredCapsForProvider(0, capabilities),
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::shm(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "shm",
            .memoryRegistrationModes = static_cast<int>(memoryRegistrationModes(capabilities)),
            .endpointType = FI_EP_RDM,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_ADDR_STR},
            .supportedProtocols = {FI_PROTO_SHM},
            .requiredCaps = libfabricRequiredCaps(capabilities),
            .filteredCaps = filteredCapsForProvider(0, capabilities),
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::efa(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "efa",
            .memoryRegistrationModes = static_cast<int>(memoryRegistrationModes(capabilities)),
            .endpointType = FI_EP_RDM,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_ADDR_EFA},
            .supportedProtocols = {FI_PROTO_EFA},
            .requiredCaps = libfabricRequiredCaps(capabilities),
            .filteredCaps = filteredCapsForProvider(FI_TAGGED, capabilities),
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    bool ProviderConfig::isSupportedFabricInfo(FabricInfoView view) const noexcept
    {
        // Filters out all protocol types that are not supported for this provider.
        auto const protocolNotSupported = std::ranges::find(_values.supportedProtocols, view->ep_attr->protocol) == _values.supportedProtocols.end();

        // Filters out all address formats that are not supported for this provider.
        auto const addressFormatNotSupported =
            std::ranges::find(_values.supportedAddressFormats, view->addr_format) == _values.supportedAddressFormats.end();

        // Filters out all info objects that have caps set that we can't support.
        auto const containsFilteredCaps = ((view->caps & _values.filteredCaps) > 0);

        // Filters out all info objects that are missing caps that we always need.
        auto const missingRequiredCaps = (_values.requiredCaps != 0) && ((view->caps & _values.requiredCaps) != _values.requiredCaps);

        // Filters out all objects that are not the endpoint type that we are looking for with this provider.
        auto const unsupportedEndpointType = (view->ep_attr->type != _values.endpointType);

        return !(protocolNotSupported || addressFormatNotSupported || containsFilteredCaps || missingRequiredCaps || unsupportedEndpointType);
    }

    std::string ProviderConfig::getProviderName() const
    {
        return _values.providerName;
    }

    int ProviderConfig::getSupportedMemoryRegistrationModes() const noexcept
    {
        return _values.memoryRegistrationModes;
    }

    ::fi_ep_type ProviderConfig::getEndpointType() const noexcept
    {
        return _values.endpointType;
    }

    std::uint64_t ProviderConfig::getCaps() const noexcept
    {
        return _values.caps;
    }

    ProviderConfig::ProviderConfig(ProviderConfigValues values, std::optional<ProviderCapabilities> capabilities)
        : _values{std::move(values)}
        , _capabilities{capabilities}
    {}
}
