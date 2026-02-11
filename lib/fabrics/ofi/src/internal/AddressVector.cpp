// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file AddressVector.cpp
 * @brief Implementation of AddressVector wrapper for libfabric address vectors
 */

#include "AddressVector.hpp"
#include <memory>
#include <utility>
#include <sys/types.h>
#include <mxl-internal/Logging.hpp>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    // Return default attributes: count=4 (expect ~4 remote endpoints), epPerNode=0 (unknown)
    AddressVector::Attributes AddressVector::Attributes::defaults() noexcept
    {
        return AddressVector::Attributes{.count = 4, .epPerNode = 0};
    }

    // Convert our Attributes struct to libfabric's fi_av_attr
    ::fi_av_attr AddressVector::Attributes::toRaw() const noexcept
    {
        ::fi_av_attr attr{};
        attr.type = FI_AV_TABLE;         // Use table-based AV (handles are integer indices)
        attr.count = count;              // Expected number of addresses
        attr.ep_per_node = epPerNode;    // Endpoints per physical node (0=unknown)
        attr.name = nullptr;             // No named AV
        attr.map_addr = nullptr;         // Let provider choose mapping
        attr.flags = 0;                  // No special flags
        return attr;
    }

    // Factory method: create and open an address vector in the given domain
    std::shared_ptr<AddressVector> AddressVector::open(std::shared_ptr<Domain> domain, Attributes attr)
    {
        fid_av* raw;

        // Convert our attributes to libfabric format
        auto fiAttr = attr.toRaw();

        // Call libfabric to open the address vector
        // fiCall throws FabricException on error
        fiCall(::fi_av_open, "Failed to open address vector", domain->raw(), &fiAttr, &raw, nullptr);

        // Work around std::make_shared needing access to private constructor
        // This pattern exposes the constructor only within this function scope
        struct MakeSharedEnabler : public AddressVector
        {
            MakeSharedEnabler(fid_av* raw, std::shared_ptr<Domain> domain)
                : AddressVector(raw, domain)
            {}
        };

        return std::make_shared<MakeSharedEnabler>(raw, domain);
    }

    // Insert a remote fabric address and get back a handle (fi_addr_t)
    fi_addr_t AddressVector::insert(FabricAddress const& addr)
    {
        // FI_ADDR_UNSPEC is the "uninitialized" sentinel value
        ::fi_addr_t fiAddr{FI_ADDR_UNSPEC};

        // Insert 1 address, get back 1 fi_addr_t handle
        // fi_av_insert returns the number of addresses successfully inserted (should be 1)
        if (auto ret = ::fi_av_insert(_raw, addr.raw(), 1, &fiAddr, 0, nullptr); ret != 1)
        {
            throw Exception::internal("Failed to insert address into the address vector. {}", ::fi_strerror(ret));
        }

        // Log the successful insertion with the human-readable address and the returned handle
        MXL_INFO("Remote endpoint address \"{}\" was added to the Address Vector with fi_addr \"{}\"", addrToString(addr), fiAddr);

        return fiAddr;
    }

    // Remove an address from the AV (noexcept - errors logged but not thrown)
    void AddressVector::remove(::fi_addr_t addr) noexcept
    {
        // Remove 1 address from the AV
        // The address parameter is a pointer to the fi_addr_t handle, not the FabricAddress
        fiCall(::fi_av_remove, "Failed to remove address from address vector", _raw, &addr, 1, 0);
    }

    // Convert a fabric address to a human-readable string for logging/debugging
    std::string AddressVector::addrToString(FabricAddress const& addr) const
    {
        std::string s;
        size_t len = 0;

        // First call with NULL buffer to query the string length
        ::fi_av_straddr(_raw, addr.raw(), nullptr, &len);

        // Allocate string with the correct size
        s.resize(len);

        // Second call to get the actual string
        auto ret = ::fi_av_straddr(_raw, addr.raw(), s.data(), &len);
        if (ret == nullptr)
        {
            throw Exception::internal("Failed to convert address to string");
        }

        return s;
    }

    // Private constructor - initializes from a raw fi_av pointer
    AddressVector::AddressVector(::fid_av* raw, std::shared_ptr<Domain> domain)
        : _raw(raw)
        , _domain(std::move(domain))  // Hold onto domain to keep it alive
    {}

    // Destructor - close the AV and release libfabric resources
    AddressVector::~AddressVector()
    {
        close();
    }

    // Move constructor - transfer ownership from other to this
    AddressVector::AddressVector(AddressVector&& other) noexcept
        : _raw(other._raw)
        , _domain(std::move(other._domain))
    {
        // Leave other in a valid but empty state
        other._raw = nullptr;
    }

    // Move assignment - close current AV, then take ownership of other's AV
    AddressVector& AddressVector::operator=(AddressVector&& other)
    {
        // Close our current AV (if any)
        close();

        // Take ownership of other's AV
        _raw = other._raw;
        other._raw = nullptr;
        _domain = std::move(other._domain);

        return *this;
    }

    // Internal helper to close the AV - safe to call multiple times
    void AddressVector::close()
    {
        if (_raw)
        {
            MXL_DEBUG("Closing address vector");

            // Call libfabric to close the AV
            // The fid (fabric identifier) is the base class of fi_av
            fiCall(::fi_close, "Failed to close address vector", &_raw->fid);
            _raw = nullptr;
        }
    }

    // Accessor for mutable raw pointer (used when binding to endpoints)
    ::fid_av* AddressVector::raw() noexcept
    {
        return _raw;
    }

    // Accessor for const raw pointer
    ::fid_av const* AddressVector::raw() const noexcept
    {
        return _raw;
    }
}
