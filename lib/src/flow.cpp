#include <cstdint>
#include <exception>
#include <string>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "internal/Instance.hpp"
#include "internal/Logging.hpp"

using namespace mxl::lib;

extern "C"
MXL_EXPORT
mxlStatus
mxlCreateFlow( mxlInstance in_instance, const char *in_flowDef, const char * /*in_options*/ )
{
    try
    {
        if ( in_flowDef != nullptr )
        {
            if (auto const instance = to_Instance( in_instance ); instance != nullptr )
            {
                instance->createFlow( in_flowDef );
                return MXL_STATUS_OK;
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch ( std::exception const& e )
    {
        MXL_ERROR( "Failed to create flow : {}", e.what() );
    }
    catch (...)
    {
        MXL_ERROR( "Failed to create flow : {}", "An unknown error occured." );
    }
    return MXL_ERR_UNKNOWN;
}

extern "C"
MXL_EXPORT
mxlStatus
mxlDestroyFlow( mxlInstance in_instance, const char *in_flowId )
{
    try
    {
        if (auto const instance = to_Instance( in_instance ); instance != nullptr)
        {
            if ( in_flowId != nullptr )
            {
                auto const id = uuids::uuid::from_string( in_flowId );
                if ( id.has_value() )
                {
                    auto const found = instance->deleteFlow( *id );
                    return ( found ) ? MXL_STATUS_OK : MXL_ERR_FLOW_NOT_FOUND;
                }
            }
        }

        return MXL_ERR_INVALID_ARG;
    }
    catch ( std::exception const& e )
    {
        MXL_ERROR( "Failed to destroy flow : {}", e.what() );
    }
    catch (...)
    {
        MXL_ERROR( "Failed to destroy flow : {}", "An unknown error occured." );
    }
    return MXL_ERR_UNKNOWN;
}

extern "C"
MXL_EXPORT
mxlStatus
mxlCreateFlowReader( mxlInstance in_instance, const char *in_flowId, const char * /*in_options*/, mxlFlowReader *out_reader )
{
    try
    {
        if (out_reader != nullptr)
        {
            if (auto const instance = to_Instance( in_instance ); instance != nullptr)
            {
                if ( (in_flowId != nullptr) && uuids::uuid::is_valid_uuid( in_flowId ) )
                {
                    *out_reader = reinterpret_cast<mxlFlowReader>( instance->createFlowReader( in_flowId ) );
                    return MXL_STATUS_OK;
                }
            }
        }

        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlDestroyFlowReader( mxlInstance in_instance, mxlFlowReader in_reader )
{
    try
    {
        if (auto const instance = to_Instance( in_instance ); instance != nullptr)
        {
            return instance->removeReader( to_FlowReaderId( in_reader ) )
                ? MXL_STATUS_OK
                : MXL_ERR_INVALID_FLOW_READER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlCreateFlowWriter( mxlInstance in_instance, const char *in_flowId, const char * /*in_options */, mxlFlowWriter *out_writer )
{
    try
    {
        if ( out_writer != nullptr )
        {
            if (auto const instance = to_Instance( in_instance ); instance != nullptr)
            {
                if ( (in_flowId != nullptr) && uuids::uuid::is_valid_uuid( in_flowId ) )
                {
                    *out_writer = reinterpret_cast<mxlFlowWriter>( instance->createFlowWriter( in_flowId ) );
                    return MXL_STATUS_OK;
                }
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlDestroyFlowWriter( mxlInstance in_instance, mxlFlowWriter in_writer )
{
    try
    {
        if (auto const instance = to_Instance( in_instance ); instance != nullptr)
        {
            return instance->removeWriter( to_FlowWriterId( in_writer ) )
                ?  MXL_STATUS_OK
                : MXL_ERR_INVALID_FLOW_WRITER;
        }

        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlFlowReaderGetInfo( mxlInstance in_instance, mxlFlowReader in_reader, FlowInfo *out_info )
{
    try
    {
        if ( out_info != nullptr )
        {
            if (auto const instance = to_Instance( in_instance ); instance != nullptr)
            {
                if (auto const reader = instance->getReader( to_FlowReaderId( in_reader ) ); reader != nullptr )
                {
                    *out_info = reader->getFlowInfo();
                    return MXL_STATUS_OK;
                }
                return MXL_ERR_INVALID_FLOW_READER;
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlFlowReaderGetGrain(
    mxlInstance in_instance, mxlFlowReader in_reader, uint64_t in_index, uint16_t in_timeoutMs, GrainInfo *out_grainInfo, uint8_t **out_payload )
{
    try
    {
        if ( (out_grainInfo != nullptr) && (out_payload != nullptr) )
        {
            if (auto const instance = to_Instance( in_instance ); instance != nullptr)
            {
                if (auto const reader = instance->getReader( to_FlowReaderId( in_reader ) ); reader != nullptr)
                {
                    return reader->getGrain( in_index, in_timeoutMs, out_grainInfo, out_payload );
                }
                return MXL_ERR_INVALID_FLOW_READER;
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlFlowWriterOpenGrain( mxlInstance in_instance, mxlFlowWriter in_writer, uint64_t in_index, GrainInfo *out_grainInfo, uint8_t **out_payload )
{
    try
    {
        if ( (out_grainInfo != nullptr) && (out_payload != nullptr) )
        {
            if (auto const instance = to_Instance( in_instance ); instance != nullptr)
            {
                if (auto const writer = instance->getWriter( to_FlowWriterId( in_writer ) ); writer != nullptr)
                {
                    return writer->openGrain( in_index, out_grainInfo, out_payload );
                }

                return MXL_ERR_INVALID_FLOW_WRITER;
            }
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlFlowWriterCancel( mxlInstance in_instance, mxlFlowWriter in_writer )
{
    try
    {
        if (auto const instance = to_Instance( in_instance ); instance != nullptr)
        {
            if (auto const writer = instance->getWriter( to_FlowWriterId( in_writer ) ); writer != nullptr)
            {
                return writer->cancel();
            }
            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus
mxlFlowWriterCommit( mxlInstance in_instance, mxlFlowWriter in_writer, const GrainInfo *in_grainInfo )
{
    try
    {
        if (auto const instance = to_Instance( in_instance ); instance != nullptr)
        {
            if (auto const writer = instance->getWriter( to_FlowWriterId( in_writer ) ); writer != nullptr)
            {
                return writer->commit( in_grainInfo );
            }
            return MXL_ERR_INVALID_FLOW_WRITER;
        }
        return MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}
