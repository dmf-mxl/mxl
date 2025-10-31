// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace mxl::lib::fabrics::ofi
{
    /** \brief Returns the libfabric version this library was compiled with.
     */
    std::uint32_t fiVersion() noexcept;
}
