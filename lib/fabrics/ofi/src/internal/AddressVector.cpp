// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "AddressVector.hpp"
#include <memory>
#include <utility>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_errno.h>
#include <sys/types.h>
#include "internal/Logging.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    AddressVector::Attributes AddressVector::Attributes::defaults() noexcept
    {
        return AddressVector::Attributes{.count = 4, .epPerNode = 0};
    }

    ::fi_av_attr AddressVector::Attributes::toRaw() const noexcept
    {
        ::fi_av_attr attr{};
        attr.type = FI_AV_TABLE;
        attr.count = count;
        attr.ep_per_node = epPerNode;
        attr.name = nullptr;
        attr.map_addr = nullptr;
        attr.flags = 0;
        return attr;
    }

    std::shared_ptr<AddressVector> AddressVector::open(std::shared_ptr<Domain> domain, Attributes attr)
    {
        fid_av* raw;

        auto fiAttr = attr.toRaw();

        fiCall(::fi_av_open, "Failed to open address vector", domain->raw(), &fiAttr, &raw, nullptr);

        // expose the private constructor to std::make_shared inside this function
        struct MakeSharedEnabler : public AddressVector
        {
            MakeSharedEnabler(fid_av* raw, std::shared_ptr<Domain> domain)
                : AddressVector(raw, domain)
            {}
        };

        return std::make_shared<MakeSharedEnabler>(raw, domain);
    }

    fi_addr_t AddressVector::insert(FabricAddress const& addr)
    {
        ::fi_addr_t fiAddr{FI_ADDR_UNSPEC};

        if (auto ret = ::fi_av_insert(_raw, addr.raw(), 1, &fiAddr, 0, nullptr); ret != 1)
        {
            throw Exception::internal("Failed to insert address into the address vector. {}", ::fi_strerror(ret));
        }
        MXL_INFO("Remote endpoint address \"{}\" was added to the Address Vector with fi_addr \"{}\"", addrToString(addr), fiAddr);

        return fiAddr;
    }

    void AddressVector::remove(::fi_addr_t addr) noexcept
    {
        fiCall(::fi_av_remove, "Failed to remove address from address vector", _raw, &addr, 1, 0);
    }

    std::string AddressVector::addrToString(FabricAddress const& addr) const
    {
        std::string s;
        size_t len = 0;

        ::fi_av_straddr(_raw, addr.raw(), nullptr, &len);
        s.resize(len);

        auto ret = ::fi_av_straddr(_raw, addr.raw(), s.data(), &len);
        if (ret == nullptr)
        {
            throw Exception::internal("Failed to convert address to string");
        }

        return s;
    }

    AddressVector::AddressVector(::fid_av* raw, std::shared_ptr<Domain> domain)
        : _raw(raw)
        , _domain(std::move(domain))
    {}

    AddressVector::~AddressVector()
    {
        close();
    }

    AddressVector::AddressVector(AddressVector&& other) noexcept
        : _raw(other._raw)
        , _domain(std::move(other._domain))
    {
        other._raw = nullptr;
    }

    AddressVector& AddressVector::operator=(AddressVector&& other)
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;
        _domain = std::move(other._domain);

        return *this;
    }

    void AddressVector::close()
    {
        if (_raw)
        {
            MXL_DEBUG("Closing address vector");

            fiCall(::fi_close, "Failed to close address vector", &_raw->fid);
            _raw = nullptr;
        }
    }

    ::fid_av* AddressVector::raw() noexcept
    {
        return _raw;
    }

    ::fid_av const* AddressVector::raw() const noexcept
    {
        return _raw;
    }
}
