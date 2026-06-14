// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#include "ProviderConfig.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        namespace
        {
            constexpr std::uint64_t const supportedMemoryRegistrationModes = FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY;
        }

        std::uint64_t libfabricCaps(std::optional<ProviderCapabilities> const& capabilities, bool isTarget)
        {
            if (!capabilities)
            {
                return 0;
            }
            auto result = std::uint64_t{0};
            if (capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE)
            {
                result |= isTarget ? FI_REMOTE_WRITE : FI_WRITE;
            }
            if (capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_SEND_RECEIVE)
            {
                result |= FI_SEND | FI_RECV;
            }
            return result | FI_RMA;
        }

        std::uint64_t libfabricRequiredCaps(std::optional<ProviderCapabilities> const& capabilities)
        {
            if (!capabilities)
            {
                return 0;
            }
            auto result = std::uint64_t{0};
            if (capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE)
            {
                result |= FI_RMA;
            }
            return result;
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
        auto values = ProviderConfigValues{
            .providerName = "tcp",
            .memoryRegistrationModes = supportedMemoryRegistrationModes,
            .endpointType = FI_EP_MSG,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_SOCK_TCP},
            .requiredCaps = libfabricRequiredCaps(capabilities) | FI_MSG,
            .filteredCaps = FI_TAGGED,
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::verbs(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "verbs",
            .memoryRegistrationModes = supportedMemoryRegistrationModes,
            .endpointType = FI_EP_MSG,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_RDMA_CM_IB_XRC},
            .requiredCaps = libfabricRequiredCaps(capabilities) | FI_MSG,
            .filteredCaps = FI_HMEM,
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::shm(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "shm",
            .memoryRegistrationModes = supportedMemoryRegistrationModes,
            .endpointType = FI_EP_RDM,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_ADDR_STR},
            .supportedProtocols = {FI_PROTO_SHM},
            .requiredCaps = libfabricRequiredCaps(capabilities),
            .filteredCaps = FI_HMEM,
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    ProviderConfig ProviderConfig::efa(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        auto values = ProviderConfigValues{
            .providerName = "efa",
            .memoryRegistrationModes = supportedMemoryRegistrationModes,
            .endpointType = FI_EP_RDM,
            .caps = libfabricCaps(capabilities, isTarget),
            .supportedAddressFormats = {FI_ADDR_EFA},
            .supportedProtocols = {FI_PROTO_EFA},
            .requiredCaps = libfabricRequiredCaps(capabilities),
            .filteredCaps = FI_HMEM | FI_TAGGED,
        };
        return ProviderConfig{std::move(values), capabilities};
    }

    bool ProviderConfig::isSupportedFabricInfo(FabricInfoView view) const noexcept
    {
        auto const protocolNotSupported = std::ranges::find(_values.supportedProtocols, view->ep_attr->protocol) == _values.supportedProtocols.end();
        auto const addressFormatNotSupported =
            std::ranges::find(_values.supportedAddressFormats, view->addr_format) == _values.supportedAddressFormats.end();
        auto const containsFilteredCaps = ((view->caps & _values.filteredCaps) != 0);
        auto const missingRequiredCaps = (_values.requiredCaps != 0) && ((view->caps & _values.requiredCaps) == 0);
        auto const unsupportedEndpointType = (view->ep_attr->type != _values.endpointType);

        return !(protocolNotSupported || addressFormatNotSupported || containsFilteredCaps || missingRequiredCaps || unsupportedEndpointType);
    }

    std::string ProviderConfig::getProviderName() const noexcept
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
        : _values(std::move(values))
        , _capabilities(capabilities)
    {}
}
