// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <bits/types/struct_iovec.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include "Domain.hpp"
#include "Region.hpp"

namespace mxl::lib::fabrics::ofi
{

    class MemoryRegion
    {
    public:
        static MemoryRegion reg(Domain& domain, Region const& region, std::uint64_t access);

        ~MemoryRegion();

        MemoryRegion(MemoryRegion const&) = delete;
        void operator=(MemoryRegion const&) = delete;

        MemoryRegion(MemoryRegion&&) noexcept;
        MemoryRegion& operator=(MemoryRegion&&) noexcept;

        ::fid_mr* raw() noexcept;
        [[nodiscard]]
        ::fid_mr const* raw() const noexcept;

        [[nodiscard]]
        void* desc() const noexcept;

        [[nodiscard]]
        std::uint64_t rkey() const noexcept;

    private:
        friend class RegisteredRegion;

    private:
        void close();

        MemoryRegion(::fid_mr* raw);

    private:
        ::fid_mr* _raw;
    };
}
