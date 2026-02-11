// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/** \file FabricVersion.hpp
 * \brief Utility to query libfabric API version.
 *
 * Provides compile-time libfabric version (FI_VERSION macro) used when building MXL.
 * Libfabric uses versioned API to maintain compatibility across releases.
 */

#pragma once

#include <cstdint>

namespace mxl::lib::fabrics::ofi
{
    /** \brief Returns the libfabric version this library was compiled with.
     *
     * Returns FI_VERSION(major, minor) encoded as uint32_t.
     */
    std::uint32_t fiVersion() noexcept;
}
