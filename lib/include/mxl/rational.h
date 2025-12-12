// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
#   include <cstdint>
#else
#   include <stdint.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define MXL_UNDEFINED_INDEX UINT64_MAX

    typedef struct mxlRational_t
    {
        int64_t numerator;
        int64_t denominator;
    } mxlRational;

#ifdef __cplusplus
}
#endif
