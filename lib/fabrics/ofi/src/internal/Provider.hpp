// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file Provider.hpp
 * \brief Enumeration of supported libfabric providers (transport implementations).
 *
 * Providers are libfabric's abstraction for different RDMA/networking transports.
 * Each provider implements the libfabric API for a specific underlying technology.
 *
 * Supported providers:
 * - TCP: Software-based RDMA emulation over TCP/IP (portable, slower)
 * - VERBS: InfiniBand/RoCE using libibverbs (high performance, Linux)
 * - EFA: AWS Elastic Fabric Adapter (custom AWS RDMA hardware)
 * - SHM: Shared memory for intra-host communication (fastest local)
 *
 * Provider selection determines:
 * - Hardware requirements (InfiniBand NIC, EFA-capable AWS instance, etc.)
 * - Performance characteristics (latency, bandwidth, CPU overhead)
 * - Available features (immediate data support, memory registration modes)
 *
 * Provider is specified when querying available fabrics with fi_getinfo().
 */

#pragma once

#include <optional>
#include <string>
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Internal representation of supported libfabric providers.
     *
     * Maps to mxlFabricsProvider in public C API.
     */
    enum class Provider
    {
        TCP,
        VERBS,
        EFA,
        SHM,
    };

    /** \brief  Convert between external and internal versions of this type
     */
    mxlFabricsProvider providerToAPI(Provider provider) noexcept;

    /** \brief Convert between external and internal versions of this type
     */
    std::optional<Provider> providerFromAPI(mxlFabricsProvider api) noexcept;

    /** \brief Parse a provider name string, and return the enum value.
     *
     * Returns std::nullopt if the string passed was not a valid provider name.
     */
    std::optional<Provider> providerFromString(std::string const& s) noexcept;
}
