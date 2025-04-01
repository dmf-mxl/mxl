#pragma once

#ifdef __cplusplus
#  include <cstdint>
#else
#  include <stdint.h>
#endif

#if defined( __GNUC__ ) || defined( __clang__ )
#  define MXL_EXPORT __attribute__((visibility("default")))
#else
#  define MXL_EXPORT
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define MXL_UNDEFINED_OFFSET UINT64_MAX

typedef enum mxlStatus
{
    MXL_STATUS_OK = 0,
    MXL_ERR_UNKNOWN = 1,
    MXL_ERR_FLOW_NOT_FOUND = 2,
    MXL_ERR_OUT_OF_RANGE = 3,
    MXL_ERR_INVALID_FLOW_READER = 4,
    MXL_ERR_INVALID_FLOW_WRITER = 5,
    MXL_ERR_TIMEOUT = 6,
    MXL_ERR_INVALID_ARG = 7,
    MXL_ERR_CONFLICT = 8
} mxlStatus;

typedef struct mxlVersionType
{
    uint16_t major;
    uint16_t minor;
    uint16_t bugfix;
    uint16_t build;
} mxlVersionType;

typedef struct Rational
{
    int64_t numerator;
    int64_t denominator;
} Rational;

MXL_EXPORT int8_t mxlGetVersion( mxlVersionType *out_version );

typedef struct mxlInstance_t *mxlInstance;

MXL_EXPORT mxlInstance mxlCreateInstance( const char *in_mxlDomain, const char *in_options );

MXL_EXPORT mxlStatus mxlDestroyInstance( mxlInstance in_instance );

#ifdef __cplusplus
}
#endif