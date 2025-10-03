// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "MemoryRegion.hpp"
#include <cstdint>
#include <random>
#include <bits/types/struct_iovec.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <sys/mman.h>
#include "internal/Logging.hpp"
#include "Domain.hpp"
#include "Exception.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{

    void setDeviceId(::fi_mr_attr& attr, Region::Location loc)
    {
        switch (loc.iface())
        {
            case FI_HMEM_CUDA: {
                attr.device.cuda = loc.id();
                return;
            }

            case FI_HMEM_SYSTEM:
            case FI_HMEM_ROCR:
            case FI_HMEM_ZE:
            case FI_HMEM_NEURON:
            case FI_HMEM_SYNAPSEAI: return;
        }
    }

    MemoryRegion MemoryRegion::reg(Domain& domain, Region const& region, std::uint64_t access)
    {
        ::fid_mr* raw;
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

        MXL_DEBUG("Registering memory region with address 0x{}, size {} and location {}",
            reinterpret_cast<void*>(region.base),
            region.size,
            region.loc.toString());

        std::uint64_t flags = 0;
        flags |= region.loc.isHost() ? 0 : FI_HMEM_DEVICE_ONLY;

        ::fi_mr_attr attr{};
        setDeviceId(attr, region.loc);

        attr.mr_iov = region.asIovec();
        attr.iov_count = 1;
        attr.access = access;
        attr.offset = 0;                // reserved to 0
        attr.requested_key = dist(gen); // creating a random key, but it can be ignored if FI_MR_PROV_KEY mr mode is set
        attr.context = nullptr;         // not used
        attr.auth_key_size = 0;
        attr.auth_key = nullptr;
        attr.iface = region.loc.iface();
        attr.hmem_data = nullptr;
        attr.page_size = 4096;  // not used'
        attr.base_mr = nullptr; // not used
        attr.sub_mr_cnt = 0;    // not used

        fiCall(fi_mr_regattr, "Failed to register memory region", domain.raw(), &attr, flags, &raw);

        struct MakeSharedEnabler : public MemoryRegion
        {
            MakeSharedEnabler(::fid_mr* raw)
                : MemoryRegion(raw)
            {}
        };

        return {raw};
    }

    MemoryRegion::~MemoryRegion()
    {
        close();
    }

    MemoryRegion::MemoryRegion(MemoryRegion&& other) noexcept
        : _raw(other._raw)
    {
        other._raw = nullptr;
    }

    MemoryRegion& MemoryRegion::operator=(MemoryRegion&& other) noexcept
    {
        close();

        _raw = other._raw;
        other._raw = nullptr;

        return *this;
    }

    void* MemoryRegion::desc() const noexcept
    {
        return fi_mr_desc(_raw);
    }

    std::uint64_t MemoryRegion::rkey() const noexcept
    {
        return fi_mr_key(_raw);
    }

    MemoryRegion::MemoryRegion(::fid_mr* raw)
        : _raw(raw)
    {}

    void MemoryRegion::close()
    {
        if (_raw)
        {
            MXL_DEBUG("Closing memory region with rkey={:x}", rkey());
            fiCall(::fi_close, "Failed to close memory region", &_raw->fid);
            _raw = nullptr;
        }
    }

}
