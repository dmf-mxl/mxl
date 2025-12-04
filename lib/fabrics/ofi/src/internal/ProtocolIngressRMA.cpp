#include "ProtocolIngressRMA.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    RMAGrainIngressProtocol::RMAGrainIngressProtocol(std::vector<Region> regions)
        : _regions(std::move(regions))
    {}

    std::vector<RemoteRegion> RMAGrainIngressProtocol::registerMemory(std::shared_ptr<Domain> domain)
    {
        if (_isMemoryRegistered)
        {
            throw Exception::invalidState("Memory is already registered.");
        }

        domain->registerRegions(_regions, FI_REMOTE_WRITE);
        return domain->remoteRegions();
    }

    void RMAGrainIngressProtocol::start(Endpoint& ep)
    {
        if (ep.domain()->usingRecvBufForCqData())
        {
            ep.recv(immDataRegion());
        }
    }

    Target::ReadResult RMAGrainIngressProtocol::processCompletion(Endpoint& ep, Completion const& completion)
    {
        if (auto data = completion.tryData(); data)
        {
            if (_immDataBuffer)
            {
                ep.recv(immDataRegion());
                return Target::ReadResult{_immDataBuffer->data};
            }
            else
            {
                return Target::ReadResult{data->data()};
            }
        }

        return {};
    }

    void RMAGrainIngressProtocol::destroy()
    {}

    LocalRegion RMAGrainIngressProtocol::immDataRegion()
    {
        if (!_immDataBuffer)
        {
            _immDataBuffer.emplace();
        }

        return _immDataBuffer->toLocalRegion();
    }

}
