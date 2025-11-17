#include "Protocol.hpp"
#include <cassert>
#include <memory>
#include "DataLayout.hpp"
#include "Domain.hpp"
#include "Exception.hpp"
#include "ProtocolEgress.hpp"
#include "ProtocolIngress.hpp"

namespace mxl::lib::fabrics::ofi
{
    std::unique_ptr<IngressProtocol> selectProtocol(std::shared_ptr<Domain> domain, DataLayout const& layout, std::vector<Region> const& dstRegions)
    {
        if (layout.isVideo())
        {
            auto proto = std::make_unique<IngressProtocolWriter>();
            domain->registerRegions(dstRegions, FI_REMOTE_WRITE);

            return proto;
        }

        throw Exception::invalidArgument("Unsupported data layout for ingress protocol selection.");
    }

    std::unique_ptr<EgressProtocol> selectProtocol(std::shared_ptr<Endpoint> ep, DataLayout const& layout, TargetInfo const& info)
    {
        if (layout.isVideo())
        {
            return std::make_unique<EgressProtocolWriter>(std::move(ep), info.remoteRegions, layout.asVideo());
        }

        throw Exception::invalidArgument("Unsupported data layout for egress protocol selection.");
    }

}
