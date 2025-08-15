#include "Fabric.hpp"
#include <memory>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include "Exception.hpp"
#include "FIInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    std::shared_ptr<Fabric> Fabric::open(FIInfoView info)
    {
        ::fid_fabric* fid;

        fiCall(::fi_fabric2, "Failed to open fabric", info.raw(), &fid, 0, nullptr);

        // expose the private constructor to std::make_shared inside this function
        struct MakeSharedEnabler : public Fabric
        {
            MakeSharedEnabler(::fid_fabric* raw, FIInfoView info)
                : Fabric(raw, info)
            {}
        };

        return std::make_shared<MakeSharedEnabler>(fid, info);
    }

    Fabric::Fabric(::fid_fabric* raw, FIInfoView info)
        : _raw(raw)
        , _info(info.owned())
    {}

    Fabric::~Fabric()
    {
        close();
    }

    Fabric::Fabric(Fabric&& other) noexcept
        : _raw(other._raw)
        , _info(other._info)
    {
        other._raw = nullptr;
    }

    Fabric& Fabric::operator=(Fabric&& other)
    {
        close();

        _raw = other._raw;
        _info = other._info;
        other._raw = nullptr;

        return *this;
    }

    ::fid_fabric* Fabric::raw() noexcept
    {
        return _raw;
    }

    ::fid_fabric const* Fabric::raw() const noexcept
    {
        return _raw;
    }

    FIInfoView Fabric::info() const noexcept
    {
        return _info.view();
    }

    void Fabric::close()
    {
        if (_raw != nullptr)
        {
            fiCall(::fi_close, "failed to close fabric", &_raw->fid);
        }
    }

}
