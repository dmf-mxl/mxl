// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * \file NoEntryPointBackend.cpp
 *
 * A library named like a payload backend that does not export the backend entry point. The tests use it to check
 * that libmxl rejects such a library.
 */

#include <mxl/platform.h>

/**
 * An exported function with a different name than the backend entry point.
 * \return Always 0.
 */
extern "C" MXL_EXPORT
int mxlTestNotABackend()
{
    return 0;
}
