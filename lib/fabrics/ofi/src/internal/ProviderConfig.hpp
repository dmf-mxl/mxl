#pragma once

#include <vector>
#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    struct ProviderConfigValues
    {
        std::string providerName;
        int memoryRegistrationModes;
        ::fi_ep_type endpointType;
        std::uint64_t caps;

        std::vector<std::uint32_t> supportedAddressFormats;
        std::vector<std::uint32_t> supportedProtocols;
        std::uint64_t requiredCaps;
        std::uint64_t filteredCaps;
    };

    class ProviderCapabilities
    {
    public:
        std::uint64_t maxMessageSize = 0;
        std::uint64_t interfaceCaps = 0;

    public:
        static ProviderCapabilities fromAPI(::mxlFabricsInterfaceCaps);
    };

    class ProviderConfig
    {
    public:
        static ProviderConfig create(Provider provider, bool, std::optional<ProviderCapabilities>);

        static ProviderConfig tcp(bool, std::optional<ProviderCapabilities>);
        static ProviderConfig verbs(bool, std::optional<ProviderCapabilities>);
        static ProviderConfig shm(bool, std::optional<ProviderCapabilities>);
        static ProviderConfig efa(bool, std::optional<ProviderCapabilities>);

    public:
        /**
         * Returns true if this is a supported fabric configuration info. Can be used to filter the output of fi_getinfo to only a list of supported
         * fabric configs.
         */
        [[nodiscard]]
        bool isSupportedFabricInfo(FabricInfoView view) const noexcept;

        /**
         * Get the provider name (as expected by libfabric in prov_name) as a string.
         */
        [[nodiscard]]
        std::string getProviderName() const noexcept;

        /**
         * Return a value that represents all supported memory registration modes (mr_mode).
         */
        [[nodiscard]]
        int getSupportedMemoryRegistrationModes() const noexcept;

        /**
         *
         */
        [[nodiscard]]
        ::fi_ep_type getEndpointType() const noexcept;

        /**
         *
         */
        std::uint64_t getCaps() const noexcept;

    private:
        ProviderConfig(ProviderConfigValues values);
        ProviderConfigValues _values;
    };
}
