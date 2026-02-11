// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Format.hpp
 * @brief Formatting utilities and fmt library specializations for fabrics types
 *
 * This file provides fmt::format support for MXL fabrics-specific types, enabling them
 * to be used directly in logging and error messages with fmt::format syntax.
 *
 * USAGE:
 * ```cpp
 * auto provider = MXL_SHARING_PROVIDER_EFA;
 * MXL_INFO("Using provider: {}", provider); // Outputs: "Using provider: efa"
 * ```
 */

#pragma once

#include <fmt/base.h>
#include <fmt/format.h>
#include <mxl/fabrics.h>
#include "Provider.hpp"

namespace ofi = mxl::lib::fabrics::ofi;

/**
 * @brief fmt::formatter specialization for mxlFabricsProvider enum
 *
 * Allows mxlFabricsProvider values to be formatted as strings in fmt::format calls.
 * Converts provider enums to their string names: AUTO->auto", TCP->"tcp", etc.
 */
template<>
struct fmt::formatter<mxlFabricsProvider>
{
    constexpr auto parse(fmt::format_parse_context& ctx)
    {
        return ctx.begin(); // No format specifiers, just default formatting
    }

    template<typename Context>
    constexpr auto format(mxlFabricsProvider const& provider, Context& ctx) const
    {
        // Map each provider enum value to its string representation
        switch (provider)
        {
            case MXL_SHARING_PROVIDER_AUTO:  return fmt::format_to(ctx.out(), "auto");
            case MXL_SHARING_PROVIDER_TCP:   return fmt::format_to(ctx.out(), "tcp");
            case MXL_SHARING_PROVIDER_VERBS: return fmt::format_to(ctx.out(), "verbs");
            case MXL_SHARING_PROVIDER_EFA:   return fmt::format_to(ctx.out(), "efa");
            case MXL_SHARING_PROVIDER_SHM:   return fmt::format_to(ctx.out(), "shm");
            default:                         return fmt::format_to(ctx.out(), "unknown");
        }
    }
};

/**
 * @brief fmt::formatter specialization for internal ofi::Provider enum
 *
 * Similar to mxlFabricsProvider formatter, but for the internal C++ Provider enum.
 * This allows logging of the internal provider representation.
 */
template<>
struct fmt::formatter<ofi::Provider>
{
    constexpr auto parse(fmt::format_parse_context& ctx)
    {
        return ctx.begin(); // No format specifiers
    }

    template<typename Context>
    constexpr auto format(ofi::Provider const& provider, Context& ctx) const
    {
        // Map each internal Provider enum value to its string representation
        switch (provider)
        {
            case ofi::Provider::TCP:   return fmt::format_to(ctx.out(), "tcp");
            case ofi::Provider::VERBS: return fmt::format_to(ctx.out(), "verbs");
            case ofi::Provider::EFA:   return fmt::format_to(ctx.out(), "efa");
            case ofi::Provider::SHM:   return fmt::format_to(ctx.out(), "shm");
            default:                   return fmt::format_to(ctx.out(), "unknown");
        }
    }
};

namespace mxl::lib::fabrics::ofi
{
    /**
     * @brief Convert a libfabric protocol code to a human-readable string
     *
     * @param protocol The libfabric protocol identifier (e.g., FI_PROTO_RDMA_CM_IB_RC)
     * @return std::string Human-readable protocol name
     *
     * Libfabric defines various protocol constants (FI_PROTO_*) that identify
     * the underlying network protocol being used. This function converts them
     * to readable strings for logging and debugging.
     */
    std::string fiProtocolToString(std::uint64_t protocol) noexcept;
}
