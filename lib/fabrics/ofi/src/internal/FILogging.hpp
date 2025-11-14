// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace mxl::lib::fabrics::ofi
{
    /** \brief Initalize logging for libfabric. After this call, all libfabric internal logs
     * will be written to the mxl logging system. It is save to call this function multiple times
     * and concurrently from different threads.
     * If this function throws an exception, the program cannot continue.
     */
    void fiInitLogging();
}
