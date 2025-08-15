#pragma once

#include <optional>
#include <string>
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{
    enum class Provider
    {
        TCP,
        VERBS,
        EFA,
    };

    /// Convert between external and internal versions of this type
    mxlFabricsProvider providerToAPI(Provider provider) noexcept;

    /// Convert between external and internal versions of this type
    std::optional<Provider> providerFromAPI(mxlFabricsProvider api) noexcept;

    /// Parse a provider name string, and return the enum value. Returns std::nullopt
    /// if the string passed was not a valid provider name.
    std::optional<Provider> providerFromString(std::string const& s) noexcept;
}
