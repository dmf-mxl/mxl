// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Address.hpp"
#include <cstdint>
#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_errno.h>
#include "mxl/mxl.h"
#include "Base64.hpp"
#include "Exception.hpp"
#include "FabricAddress.hpp"

namespace mxl::lib::fabrics::ofi
{
    RawFabricAddress::RawFabricAddress(std::vector<std::uint8_t> addr, FabricAddressFormat addressFormat)
        : _inner(std::move(addr))
        , _addressFormat(addressFormat)
    {}

    RawFabricAddress RawFabricAddress::fromFid(::fid_t fid, FabricInfoView info)
    {
        return retrieveFabricAddress(fid, info);
    }

    std::string RawFabricAddress::toBase64() const
    {
        return base64::encode_into<std::string>(_inner.cbegin(), _inner.cend());
    }

    void* RawFabricAddress::raw() noexcept
    {
        return _inner.data();
    }

    void const* RawFabricAddress::raw() const noexcept
    {
        return _inner.data();
    }

    std::size_t RawFabricAddress::size() const noexcept
    {
        return _inner.size();
    }

    FabricAddressFormat RawFabricAddress::format() const noexcept
    {
        return _addressFormat;
    }

    bool RawFabricAddress::operator==(RawFabricAddress const& other) const noexcept
    {
        return (_inner == other._inner) && (_addressFormat == other._addressFormat);
    }

    FabricAddress RawFabricAddress::decode() const
    {
        return FabricAddress::decode(_addressFormat, _inner.data(), _inner.size());
    }

    RawFabricAddress RawFabricAddress::fromBase64(std::string_view data, FabricAddressFormat addressFormat)
    {
        auto decoded = base64::decode_into<std::vector<std::uint8_t>>(data);
        if (decoded.empty())
        {
            throw std::runtime_error("Failed to decode base64 data into FabricAddress.");
        }
        return RawFabricAddress{std::move(decoded), addressFormat};
    }

    RawFabricAddress RawFabricAddress::retrieveFabricAddress(::fid_t fid, FabricInfoView info)
    {
        auto const format = mustConvertAddressFormat(info->addr_format);

        // First obtain the address length
        std::size_t addrlen = 0;
        auto ret = fi_getname(fid, nullptr, &addrlen);
        if (ret != -FI_ETOOSMALL)
        {
            throw FabricException("Failed to get address length from endpoint.", MXL_ERR_UNKNOWN, ret);
        }

        // Now that we have the address length, allocate a receiving buffer and call fi_getname again to retrieve the actual address
        std::vector<std::uint8_t> addr(addrlen);
        fiCall(fi_getname, "Failed to retrieve endpoint's local address.", fid, addr.data(), &addrlen);

        return RawFabricAddress{addr, format};
    }

}
