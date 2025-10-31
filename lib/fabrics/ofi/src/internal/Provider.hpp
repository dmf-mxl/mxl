// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{

    /** \brief Internal representation of supported libfabric providers.
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
