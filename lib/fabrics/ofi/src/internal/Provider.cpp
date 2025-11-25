// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Provider.hpp"
#include <map>
#include <optional>
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{

    static std::map<std::string_view, Provider> const providerStringMap = {
        {"tcp",   Provider::TCP  },
        {"verbs", Provider::VERBS},
        {"efa",   Provider::EFA  },
        {"shm",   Provider::SHM  },
    };

    mxlFabricsProvider providerToAPI(Provider provider) noexcept
    {
        switch (provider)
        {
            case Provider::TCP:   return MXL_FABRICS_PROVIDER_TCP;
            case Provider::VERBS: return MXL_FABRICS_PROVIDER_VERBS;
            case Provider::EFA:   return MXL_FABRICS_PROVIDER_EFA;
            case Provider::SHM:   return MXL_FABRICS_PROVIDER_SHM;
        }

        return MXL_FABRICS_PROVIDER_AUTO;
    }

    std::optional<Provider> providerFromAPI(mxlFabricsProvider api) noexcept
    {
        switch (api)
        {
            case MXL_FABRICS_PROVIDER_AUTO:
            case MXL_FABRICS_PROVIDER_TCP:   return Provider::TCP;
            case MXL_FABRICS_PROVIDER_VERBS: return Provider::VERBS;
            case MXL_FABRICS_PROVIDER_EFA:   return Provider::EFA;
            case MXL_FABRICS_PROVIDER_SHM:   return Provider::SHM;
        }

        return std::nullopt;
    }

    std::optional<Provider> providerFromString(std::string const& s) noexcept
    {
        auto it = providerStringMap.find(s);
        if (it == providerStringMap.end())
        {
            return std::nullopt;
        }

        return it->second;
    }
}
