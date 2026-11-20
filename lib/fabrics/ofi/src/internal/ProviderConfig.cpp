// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#include "ProviderConfig.hpp"
#include <algorithm>
#include <string>
#include "mxl/fabrics.h"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        namespace
        {
            constexpr auto const supportedMemoryRegistrationModes = std::uint64_t{FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY};
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

        [[nodiscard]]
        bool requiresHmem(std::optional<ProviderCapabilities> const& capabilities) noexcept
        {
            return capabilities && ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_HMEM) != 0);
        }

        /** Memory registration modes for a provider. Device grain payloads also need FI_MR_HMEM. */
        int memoryRegistrationModes(std::optional<ProviderCapabilities> const& capabilities) noexcept
        {
            auto modes = supportedMemoryRegistrationModes;
            if (requiresHmem(capabilities))
            {
                modes |= FI_MR_HMEM;
            }
            return static_cast<int>(modes);
        }

        /**
         * Host-only setups reject FI_HMEM domains, so they keep the non-HMEM path. When HMEM is required (device grain payloads), FI_HMEM
         * domains are kept. On the discovery path (no capabilities), FI_HMEM is not filtered, so callers can find HMEM-capable interfaces
         * with mxlFabricsGetInterfaces().
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
        // With concrete capabilities, FI_MSG goes into the hints so that returned fi_info objects advertise it. Discovery keeps the
        // hints unchanged.
        auto const msgCap = capabilities ? static_cast<std::uint64_t>(FI_MSG) : 0;
        auto values = ProviderConfigValues{
            .providerName = "tcp",
            .memoryRegistrationModes = memoryRegistrationModes(capabilities),
            .endpointType = FI_EP_MSG,
            .caps = libfabricCaps(capabilities, isTarget) | msgCap,
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_SOCK_TCP},
            .requiredCaps = libfabricRequiredCaps(capabilities) | FI_MSG,
            .filteredCaps = filteredCapsForProvider(FI_TAGGED, capabilities),
            .supportedFabricNames = {},
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::verbs(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        // With concrete capabilities, FI_MSG goes into the hints so that HMEM-capable domains are not rejected for lacking it.
        auto const msgCap = capabilities ? static_cast<std::uint64_t>(FI_MSG) : 0;
        auto values = ProviderConfigValues{
            .providerName = "verbs",
            .memoryRegistrationModes = memoryRegistrationModes(capabilities),
            .endpointType = FI_EP_MSG,
            .caps = libfabricCaps(capabilities, isTarget) | msgCap,
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_RDMA_CM_IB_RC},
            .requiredCaps = libfabricRequiredCaps(capabilities) | FI_MSG,
            .filteredCaps = filteredCapsForProvider(0, capabilities),
            .supportedFabricNames = {},
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::shm(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "shm",
            .memoryRegistrationModes = memoryRegistrationModes(capabilities),
            .endpointType = FI_EP_RDM,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_ADDR_STR},
            .supportedProtocols = {FI_PROTO_SHM},
            .requiredCaps = libfabricRequiredCaps(capabilities),
            .filteredCaps = filteredCapsForProvider(0, capabilities),
            .supportedFabricNames = {},
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::efa(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "efa",
            .memoryRegistrationModes = memoryRegistrationModes(capabilities),
            .endpointType = FI_EP_RDM,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_ADDR_EFA},
            .supportedProtocols = {FI_PROTO_EFA},
            .requiredCaps = libfabricRequiredCaps(capabilities),
            .filteredCaps = filteredCapsForProvider(0, capabilities),
            // EFA publishes two RDM fabrics for the same device, and only `efa` is supported for now.
            .supportedFabricNames = {"efa"},
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

        // Filters out all fabrics whose name is not in supportedFabricNames, if any are given.
        auto const unsupportedFabricName =
            !_values.supportedFabricNames.empty() &&
            ((view->fabric_attr->name == nullptr) ||
                std::ranges::find(_values.supportedFabricNames, std::string{view->fabric_attr->name}) == _values.supportedFabricNames.end());

        return !(protocolNotSupported || addressFormatNotSupported || containsFilteredCaps || missingRequiredCaps || unsupportedEndpointType ||
                 unsupportedFabricName);
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
