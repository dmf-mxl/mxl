// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
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

    typedef struct Rational
    {
        int64_t numerator;
        int64_t denominator;
    } Rational;

#ifdef __cplusplus
}
#endif
