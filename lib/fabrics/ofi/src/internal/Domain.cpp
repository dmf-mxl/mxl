#include "Domain.hpp"
#include <memory>
#include <rdma/fabric.h>
#include "Exception.hpp"
#include "Fabric.hpp"

namespace mxl::lib::fabrics::ofi
{
    Domain::Domain(::fid_domain* raw, std::shared_ptr<Fabric> fabric)
        : _raw(raw)
        , _fabric(std::move(fabric))
    {}

    Domain::~Domain()
    {
        close();
    }

    std::shared_ptr<Domain> Domain::open(std::shared_ptr<Fabric> fabric)
    {
        ::fid_domain* domain;

        fiCall(::fi_domain2, "Failed to open domain", fabric->raw(), fabric->info().raw(), &domain, 0, nullptr);

        struct MakeSharedEnabler : public Domain
        {
            MakeSharedEnabler(::fid_domain* domain, std::shared_ptr<Fabric> fabric)
                : Domain(domain, std::move(fabric))
            {}
        };

        return std::make_shared<MakeSharedEnabler>(domain, std::move(fabric));
    }

    void Domain::close()
    {
        if (_raw != nullptr)
        {
            fiCall(::fi_close, "Failed to close domain", &_raw->fid);
            _raw = nullptr;
        }
    }

    Domain::Domain(Domain&& other) noexcept
        : _raw(other._raw)
        , _fabric(std::move(other._fabric))
    {
        other._raw = nullptr;
    }

    Domain& Domain::operator=(Domain&& other)
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;

        _fabric = std::move(other._fabric);

        return *this;
    }

    ::fid_domain* Domain::raw() noexcept
    {
        return _raw;
    }

    ::fid_domain const* Domain::raw() const noexcept
    {
        return _raw;
    }

    bool Domain::usingVirtualAddresses() const noexcept
    {
        return (_fabric->info().raw()->domain_attr->mr_mode & FI_MR_VIRT_ADDR) != 0;
    }

    bool Domain::usingRecvBufForCqData() const noexcept
    {
        return (_fabric->info().raw()->domain_attr->mode & FI_RX_CQ_DATA) != 0;
    }

}
