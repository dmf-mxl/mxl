#include <cstdint>
#include <exception>
#include <memory>
#include <mxl/mxl.h>
#include <mxl/version.h>
#include <string>
#include "internal/Instance.hpp"

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
        auto tmp = new mxl::lib::InstanceInternal{ std::make_unique<mxl::lib::Instance>( in_mxlDomain, opts ) };
        return reinterpret_cast<mxlInstance>( tmp );
    }
    catch (...)
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
        auto const instance = reinterpret_cast<mxl::lib::InstanceInternal *>( in_instance );
        delete instance;

        return (instance != nullptr)
            ? MXL_STATUS_OK
            : MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}