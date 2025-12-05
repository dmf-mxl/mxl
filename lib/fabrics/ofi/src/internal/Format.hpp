// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/base.h>
#include <fmt/format.h>
#include <mxl/fabrics.h>
#include "Provider.hpp"

namespace ofi = mxl::lib::fabrics::ofi;

template<>
struct fmt::formatter<mxlFabricsProvider>
{
    constexpr auto parse(fmt::format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template<typename Context>
    constexpr auto format(mxlFabricsProvider const& provider, Context& ctx) const
    {
        switch (provider)
        {
            case MXL_FABRICS_PROVIDER_AUTO:  return fmt::format_to(ctx.out(), "auto");
            case MXL_FABRICS_PROVIDER_TCP:   return fmt::format_to(ctx.out(), "tcp");
            case MXL_FABRICS_PROVIDER_VERBS: return fmt::format_to(ctx.out(), "verbs");
            case MXL_FABRICS_PROVIDER_EFA:   return fmt::format_to(ctx.out(), "efa");
            case MXL_FABRICS_PROVIDER_SHM:   return fmt::format_to(ctx.out(), "shm");
            default:                         return fmt::format_to(ctx.out(), "unknown");
        }
    }
};

template<>
struct fmt::formatter<ofi::Provider>
{
    constexpr auto parse(fmt::format_parse_context& ctx)
    {
        return ctx.begin();
    }

    template<typename Context>
    constexpr auto format(ofi::Provider const& provider, Context& ctx) const
    {
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
