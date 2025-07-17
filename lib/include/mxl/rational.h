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
