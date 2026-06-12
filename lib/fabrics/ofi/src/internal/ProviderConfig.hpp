// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>
#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    /**
     * Libfabric-level configuration values for a specific provider. These are derived from MXL capability flags
     * and control how fi_getinfo results are matched and filtered.
     */
    class ProviderConfigValues
    {
    public:
        /** Provider name as expected by libfabric (e.g. "tcp", "verbs", "shm", "efa"). */
        std::string providerName;
        /** Bitmask of supported memory registration modes (FI_MR_*). */
        int memoryRegistrationModes;
        /** Endpoint type for this provider (FI_EP_MSG or FI_EP_RDM). */
        ::fi_ep_type endpointType;
        /**
         * Libfabric capability flags (FI_WRITE, FI_REMOTE_WRITE, FI_SEND, FI_RECV, etc.)
         * derived from MXL interface capabilities. Set to 0 on the query path.
         */
        std::uint64_t caps;

        /** Address formats accepted by this provider (FI_SOCKADDR_IN, FI_ADDR_STR, etc.). */
        std::vector<std::uint32_t> supportedAddressFormats;
        /** Protocols accepted by this provider (FI_PROTO_SOCK_TCP, FI_PROTO_RDMA_CM_IB_XRC, etc.). */
        std::vector<std::uint32_t> supportedProtocols;
        /**
         * Libfabric capabilities that must be present in fi_info for the interface to be accepted.
         * Derived from MXL capability flags. Zero means no requirement check.
         */
        std::uint64_t requiredCaps;
        /** Libfabric capabilities that cause an interface to be rejected if present. */
        std::uint64_t filteredCaps;
    };

    /**
     * MXL-level capability description for a provider, converted from the public API type.
     * On the query path this is nullopt (no filtering). On the setup path it carries the
     * user-requested capabilities after normalization.
     */
    class ProviderCapabilities
    {
    public:
        /** The maximum message size supported by this provider on this interface */
        std::uint64_t maxMessageSize = 0;
        /** Bitmask of MXL_FABRICS_IFACE_CAP_* flags. */
        std::uint64_t interfaceCaps = 0;

    public:
        static ProviderCapabilities fromAPI(::mxlFabricsInterfaceCaps);
    };

    class ProviderConfig
    {
    public:
        /**
         * Create a provider config for the given provider. When \p capabilities is nullopt (query path),
         * caps and requiredCaps are zero so no fi_info entries are rejected on capability grounds.
         * When present (setup path), libfabric caps are derived from the MXL capability flags.
         * \param isTarget true for target (FI_REMOTE_WRITE), false for initiator (FI_WRITE).
         */
        static ProviderConfig create(Provider provider, bool isTarget, std::optional<ProviderCapabilities> capabilities);

        static ProviderConfig tcp(bool isTarget, std::optional<ProviderCapabilities> capabilities);
        static ProviderConfig verbs(bool isTarget, std::optional<ProviderCapabilities> capabilities);
        static ProviderConfig shm(bool isTarget, std::optional<ProviderCapabilities> capabilities);
        static ProviderConfig efa(bool isTarget, std::optional<ProviderCapabilities> capabilities);

    public:
        /**
         * Returns true if this is a supported fabric configuration info. Can be used to filter the output of fi_getinfo to only a list of supported
         * fabric configs.
         */
        [[nodiscard]]
        bool isSupportedFabricInfo(FabricInfoView view) const noexcept;

        /** \brief Get the provider name (as expected by libfabric in prov_name) as a string. */
        [[nodiscard]]
        std::string getProviderName() const noexcept;

        /** \brief Return a value that represents all supported memory registration modes (mr_mode). */
        [[nodiscard]]
        int getSupportedMemoryRegistrationModes() const noexcept;

        /** \brief Return the endpoint type for this provider (FI_EP_MSG, FI_EP_RDM, etc.). */
        [[nodiscard]]
        ::fi_ep_type getEndpointType() const noexcept;

        /** \brief Return the libfabric capability flags for this provider configuration. */
        [[nodiscard]]
        std::uint64_t getCaps() const noexcept;

    private:
        ProviderConfig(ProviderConfigValues values, std::optional<ProviderCapabilities> capabilities);
        ProviderConfigValues _values;
        std::optional<ProviderCapabilities> _capabilities;
    };
}
