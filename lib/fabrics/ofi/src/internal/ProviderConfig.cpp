#include "ProviderConfig.hpp"
#include "mxl-internal/Logging.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        void validateCapabilities(std::optional<ProviderCapabilities> const& capabilities)
        {
            if (!capabilities || capabilities->interfaceCaps == 0)
            {
                MXL_WARN("No transfer capabilities requested, defaulting to REMOTE_WRITE");
            }
            else if ((capabilities->interfaceCaps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) == 0)
            {
                throw Exception::noFabric("Unsupported provider constraints. Only REMOTE_WRITE supported at this time.");
            }
        }
    }

    ProviderCapabilities ProviderCapabilities::fromAPI(::mxlFabricsInterfaceCaps caps)
    {
        return ProviderCapabilities{
            .maxMessageSize = caps.maxMessageSize,
            .interfaceCaps = caps.flags,
        };
    }

    ProviderConfig ProviderConfig::create(Provider provider, bool isTarget, std::optional<ProviderCapabilities> caps)
    {
        switch (provider)
        {
            case Provider::TCP:   return ProviderConfig::tcp(isTarget, caps);
            case Provider::VERBS: return ProviderConfig::verbs(isTarget, caps);
            case Provider::EFA:   return ProviderConfig::efa(isTarget, caps);
            case Provider::SHM:   return ProviderConfig::shm(isTarget, caps);
            default:              throw Exception::invalidState("cannot create provider config for ANY provider");
        }
    }

    ProviderConfig ProviderConfig::tcp(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        validateCapabilities(capabilities);
        auto values = ProviderConfigValues{
            .providerName = "tcp",
            .memoryRegistrationModes = FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY,
            .endpointType = FI_EP_MSG,
            .caps = isTarget ? FI_REMOTE_WRITE : FI_WRITE,
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_SOCK_TCP},
            .requiredCaps = FI_RMA | FI_MSG,
            .filteredCaps = FI_TAGGED,
        };
        return ProviderConfig{std::move(values)};
    }

    ProviderConfig ProviderConfig::verbs(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        validateCapabilities(capabilities);
        auto values = ProviderConfigValues{
            .providerName = "verbs",
            .memoryRegistrationModes = FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY,
            .endpointType = FI_EP_MSG,
            .caps = isTarget ? FI_REMOTE_WRITE : FI_WRITE,
            .supportedAddressFormats = {FI_SOCKADDR_IN, FI_SOCKADDR_IN6},
            .supportedProtocols = {FI_PROTO_RDMA_CM_IB_XRC},
            .requiredCaps = FI_RMA | FI_MSG,
            .filteredCaps = FI_HMEM,
        };
        return ProviderConfig{std::move(values)};
    }

    ProviderConfig ProviderConfig::shm(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        validateCapabilities(capabilities);
        auto values = ProviderConfigValues{
            .providerName = "shm",
            .memoryRegistrationModes = FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY,
            .endpointType = FI_EP_RDM,
            .caps = isTarget ? FI_REMOTE_WRITE : FI_WRITE,
            .supportedAddressFormats = {FI_ADDR_STR},
            .supportedProtocols = {FI_PROTO_SHM},
            .requiredCaps = FI_RMA,
            .filteredCaps = FI_HMEM,
        };
        return ProviderConfig{std::move(values)};
    }

    ProviderConfig ProviderConfig::efa(bool isTarget, std::optional<ProviderCapabilities> capabilities)
    {
        validateCapabilities(capabilities);
        auto values = ProviderConfigValues{
            .providerName = "efa",
            .memoryRegistrationModes = FI_MR_VIRT_ADDR | FI_MR_LOCAL | FI_MR_ALLOCATED | FI_MR_PROV_KEY,
            .endpointType = FI_EP_RDM,
            .caps = isTarget ? FI_REMOTE_WRITE : FI_WRITE,
            .supportedAddressFormats = {FI_ADDR_EFA},
            .supportedProtocols = {FI_PROTO_EFA},
            .requiredCaps = FI_RMA,
            .filteredCaps = FI_HMEM,
        };
        return ProviderConfig{std::move(values)};
    }

    bool ProviderConfig::isSupportedFabricInfo(FabricInfoView view) const noexcept
    {
        auto const protocolNotSupported = std::ranges::find(_values.supportedProtocols, view->ep_attr->protocol) == _values.supportedProtocols.end();
        auto const addressFormatNotSupported =
            std::ranges::find(_values.supportedAddressFormats, view->addr_format) == _values.supportedAddressFormats.end();
        auto const containsFilteredCaps = ((view->caps & _values.filteredCaps) != 0);
        auto const missingRequiredCaps = ((view->caps & _values.requiredCaps) == 0);
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

    ProviderConfig::ProviderConfig(ProviderConfigValues values)
        : _values(std::move(values))
    {}
}
