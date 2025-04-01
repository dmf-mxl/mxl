#include <cstdint>
#include <exception>
#include <memory>
#include <mxl/mxl.h>
#include <mxl/version.h>
#include <string>
#include "internal/Instance.hpp"

using namespace mxl::lib;

extern "C"
MXL_EXPORT
int8_t
mxlGetVersion( mxlVersionType *out_version )
{
    if ( out_version != nullptr )
    {
        out_version->major = MXL_VERSION_MAJOR;
        out_version->minor = MXL_VERSION_MINOR;
        out_version->bugfix = MXL_VERSION_PATCH;
        out_version->build = MXL_VERSION_BUILD;
        return 0;
    }
    else
    {
        return 1;
    }
}

extern "C"
MXL_EXPORT
mxlInstance
mxlCreateInstance( const char *in_mxlDomain, const char *in_options )
{
    try
    {
        std::string opts = ( in_options ) ? in_options : "";
        auto tmp = new InstanceInternal{ std::make_unique<Instance>( in_mxlDomain, opts ) };
        return reinterpret_cast<mxlInstance>( tmp );
    }
    catch ( std::exception & )
    {
        return nullptr;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlDestroyInstance( mxlInstance in_instance )
{
    try
    {
        auto *instance = reinterpret_cast<InstanceInternal *>( in_instance );
        if ( instance )
        {
            delete ( instance );
            return MXL_STATUS_OK;
        }
        else
        {
            return MXL_ERR_INVALID_ARG;
        }
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}