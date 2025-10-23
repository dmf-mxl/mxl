// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <rdma/fabric.h>
#include "FabricInfo.hpp"

namespace mxl::lib::fabrics::ofi
{
    class Fabric
    {
    public:
        ~Fabric();

        Fabric(Fabric const&) = delete;
        void operator=(Fabric const&) = delete;

        Fabric(Fabric&&) noexcept;
        Fabric& operator=(Fabric&&);

        ::fid_fabric* raw() noexcept;
        [[nodiscard]]
        ::fid_fabric const* raw() const noexcept;

        static std::shared_ptr<Fabric> open(FabricInfoView info);

        [[nodiscard]]
        FabricInfoView info() const noexcept;

    private:
        void close();

        Fabric(::fid_fabric* raw, FabricInfoView info);

    private:
        ::fid_fabric* _raw;
        FabricInfo _info;
    };

}
