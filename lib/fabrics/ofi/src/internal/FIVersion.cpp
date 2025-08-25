// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FIVersion.hpp"
#include <rdma/fabric.h>

namespace mxl::lib::fabrics::ofi
{
    ::uint32_t fiVersion() noexcept
    {
        return FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION);
    }
}
