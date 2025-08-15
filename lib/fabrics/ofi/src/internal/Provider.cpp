#include "Provider.hpp"
#include <map>

namespace mxl::lib::fabrics::ofi
{

    static std::map<std::string_view, Provider> const providerStringMap = {
        {"tcp",   Provider::TCP  },
        {"verbs", Provider::VERBS},
        {"efa",   Provider::EFA  }
    };

    mxlFabricsProvider providerToAPI(Provider provider) noexcept
    {
        switch (provider)
        {
            case Provider::TCP:   return MXL_SHARING_PROVIDER_TCP;
            case Provider::VERBS: return MXL_SHARING_PROVIDER_VERBS;
            case Provider::EFA:   return MXL_SHARING_PROVIDER_EFA;
            default:              return MXL_SHARING_PROVIDER_AUTO;
        }
    }

    std::optional<Provider> providerFromAPI(mxlFabricsProvider api) noexcept
    {
        switch (api)
        {
            case MXL_SHARING_PROVIDER_AUTO:
            case MXL_SHARING_PROVIDER_TCP:   return Provider::TCP;
            case MXL_SHARING_PROVIDER_VERBS: return Provider::VERBS;
            case MXL_SHARING_PROVIDER_EFA:   return Provider::EFA;
            default:                         return std::nullopt;
        }
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
