// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Address.cpp
 * @brief Implementation of FabricAddress wrapper class
 *
 * Provides RAII management of libfabric endpoint addresses, including:
 * - Retrieval from endpoints via fi_getname()
 * - Serialization to/from base64 strings for out-of-band transmission
 * - Memory management of variable-length address data
 */

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
    // Private constructor - move-constructs the internal byte vector
    FabricAddress::FabricAddress(std::vector<std::uint8_t> addr)
        : _inner(std::move(addr))
    {}

    // Factory method: create address from a libfabric object handle
    FabricAddress FabricAddress::fromFid(::fid_t fid)
    {
        return retrieveFabricAddress(fid);
    }

    // Serialize address to base64 string for out-of-band transmission
    std::string FabricAddress::toBase64() const
    {
        // Use base64 library to encode the raw bytes
        return base64::encode_into<std::string>(_inner.cbegin(), _inner.cend());
    }

    // Mutable access to raw address bytes (for passing to libfabric)
    void* FabricAddress::raw() noexcept
    {
        return _inner.data();
    }

    // Const access to raw address bytes
    void const* FabricAddress::raw() const noexcept
    {
        return _inner.data();
    }

    // Return the size in bytes of the address
    std::size_t FabricAddress::size() const noexcept
    {
        return _inner.size();
    }

    // Equality comparison - addresses are equal if their bytes are identical
    bool FabricAddress::operator==(FabricAddress const& other) const noexcept
    {
        return _inner == other._inner;
    }

    // Factory method: deserialize address from base64 string
    FabricAddress FabricAddress::fromBase64(std::string_view data)
    {
        // Decode the base64 string into a byte vector
        auto decoded = base64::decode_into<std::vector<std::uint8_t>>(data);
        if (decoded.empty())
        {
            throw std::runtime_error("Failed to decode base64 data into FabricAddress.");
        }
        return FabricAddress{std::move(decoded)};
    }

    // Internal implementation: query endpoint address using fi_getname()
    FabricAddress FabricAddress::retrieveFabricAddress(::fid_t fid)
    {
        // STEP 1: Query the address length
        // Call fi_getname with NULL buffer - this returns -FI_ETOOSMALL and sets addrlen
        size_t addrlen = 0;
        auto ret = fi_getname(fid, nullptr, &addrlen);
        if (ret != -FI_ETOOSMALL)
        {
            // We expect -FI_ETOOSMALL; any other return value is an error
            throw FabricException("Failed to get address length from endpoint.", MXL_ERR_UNKNOWN, ret);
        }

        // STEP 2: Allocate a buffer and retrieve the actual address
        std::vector<std::uint8_t> addr(addrlen);
        // fiCall is a helper macro that checks return values and throws on error
        fiCall(fi_getname, "Failed to retrieve endpoint's local address.", fid, addr.data(), &addrlen);

        return FabricAddress{addr};
    }

}
