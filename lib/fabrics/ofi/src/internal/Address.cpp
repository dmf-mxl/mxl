#include "Address.hpp"
#include <cstdint>
#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_errno.h>
#include "mxl/mxl.h"
#include "Base64.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    FabricAddress::FabricAddress(std::vector<uint8_t> addr)
        : _inner(std::move(addr))
    {}

    FabricAddress FabricAddress::fromFid(::fid_t fid)
    {
        return retrieveFabricAddress(fid);
    }

    std::string FabricAddress::toBase64() const
    {
        return base64::encode_into<std::string>(_inner.cbegin(), _inner.cend());
    }

    void* FabricAddress::raw() noexcept
    {
        return _inner.data();
    }

    void const* FabricAddress::raw() const noexcept
    {
        return _inner.data();
    }

    std::size_t FabricAddress::size() const noexcept
    {
        return _inner.size();
    }

    FabricAddress FabricAddress::fromBase64(std::string_view data)
    {
        return FabricAddress{base64::decode_into<std::vector<uint8_t>>(data)};

        auto decoded = base64::decode_into<std::vector<uint8_t>>(data);
        if (decoded.empty())
        {
            throw std::runtime_error("Failed to decode base64 data into FabricAddress");
        }
        return FabricAddress{std::move(decoded)};
    }

    FabricAddress FabricAddress::retrieveFabricAddress(::fid_t fid)
    {
        // First obtain the address length
        size_t addrlen = 0;
        auto ret = fi_getname(fid, nullptr, &addrlen);
        if (ret != -FI_ETOOSMALL)
        {
            throw FIException("Failed to get address length from endpoint.", MXL_ERR_UNKNOWN, ret);
        }

        // Now that we have the address length, allocate a receiving buffer and call fi_getname again to retrieve the actual address
        std::vector<uint8_t> addr(addrlen);
        fiCall(fi_getname, "Failed to retrieve endpoint's local address.", fid, addr.data(), &addrlen);

        return FabricAddress{addr};
    }

}
