#include "internal/Instance.hpp"
#include "internal/Logging.hpp"

#include <cstdint>
#include <exception>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <string>
#include <uuid.h>

using namespace mxl::lib;

mxlStatus
mxlCreateFlow( mxlInstance in_instance, const char *in_flowDef, const char * /*in_options*/, FlowInfo *out_flowInfo )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( in_flowDef == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( out_flowInfo != nullptr )
        {
            auto flowData = instance->createFlow( in_flowDef );
            *out_flowInfo = flowData->flow->get()->info;
        }
        return MXL_STATUS_OK;
    }
    catch ( std::exception &e )
    {
        MXL_ERROR( "Failed to create flow : {}", e.what() );
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlDestroyFlow( mxlInstance in_instance, const char *in_flowId )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( in_flowId == nullptr || !uuids::uuid::is_valid_uuid( in_flowId ) )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto id = uuids::uuid::from_string( in_flowId );
        if ( !id.has_value() )
        {
            return MXL_ERR_INVALID_ARG;
        }

        bool found = instance->deleteFlow( *id );
        return ( found ) ? MXL_STATUS_OK : MXL_ERR_FLOW_NOT_FOUND;
    }
    catch ( std::exception &e )
    {
        MXL_ERROR( "Failed to destroy flow : {}", e.what() );
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlCreateFlowReader( mxlInstance in_instance, const char *in_flowId, const char * /*in_options*/, mxlFlowReader *out_reader )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( out_reader == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( in_flowId == nullptr || !uuids::uuid::is_valid_uuid( in_flowId ) )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto id = uuids::uuid::from_string( in_flowId );
        if ( !id.has_value() )
        {
            return MXL_ERR_INVALID_ARG;
        }

        *out_reader = reinterpret_cast<mxlFlowReader>( instance->createFlowReader( in_flowId ) );

        return MXL_STATUS_OK;
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlDestroyFlowReader( mxlInstance in_instance, mxlFlowReader in_reader )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( instance->removeReader( to_FlowReaderId( in_reader ) ) )
        {
            return MXL_STATUS_OK;
        }
        else
        {
            return MXL_ERR_INVALID_FLOW_READER;
        }
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlCreateFlowWriter( mxlInstance in_instance, const char *in_flowId, const char *in_options, mxlFlowWriter *out_writer )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( out_writer == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( in_flowId == nullptr || !uuids::uuid::is_valid_uuid( in_flowId ) )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto id = uuids::uuid::from_string( in_flowId );
        if ( !id.has_value() )
        {
            return MXL_ERR_INVALID_ARG;
        }

        std::string opts;
        if ( in_options != nullptr )
        {
            opts = in_options;
        }
        else
        {
            opts = "{}";
        }

        *out_writer = reinterpret_cast<mxlFlowWriter>( instance->createFlowWriter( in_flowId ) );
        return MXL_STATUS_OK;
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlDestroyFlowWriter( mxlInstance in_instance, mxlFlowWriter in_writer )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        if ( instance->removeWriter( to_FlowWriterId( in_writer ) ) )
        {
            return MXL_STATUS_OK;
        }
        else
        {
            return MXL_ERR_INVALID_FLOW_WRITER;
        }
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlFlowReaderGetInfo( mxlInstance in_instance, mxlFlowReader in_reader, FlowInfo *out_info )
{
    try
    {
        if ( in_instance == nullptr || out_info == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto reader = instance->getReader( to_FlowReaderId( in_reader ) );
        if ( !reader )
        {
            return MXL_ERR_INVALID_FLOW_READER;
        }

        *out_info = reader->getFlowInfo();

        return MXL_STATUS_OK;
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlFlowReaderGetGrain( mxlInstance in_instance,
                       mxlFlowReader in_reader,
                       uint64_t in_index,
                       uint16_t in_timeoutMs,
                       GrainInfo *out_grainInfo,
                       GrainAccessor *out_accessor )
{
    try
    {
        if ( in_instance == nullptr || out_grainInfo == nullptr || out_accessor == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto reader = instance->getReader( to_FlowReaderId( in_reader ) );
        if ( !reader )
        {
            return MXL_ERR_INVALID_FLOW_READER;
        }
        return reader->getGrain( in_index, in_timeoutMs, out_grainInfo, out_accessor );
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlFlowWriterOpenGrain( mxlInstance in_instance, mxlFlowWriter in_writer, uint64_t in_index, GrainInfo *inout_grainInfo, uint8_t **out_payload )
{
    try
    {
        if ( in_instance == nullptr || inout_grainInfo == nullptr || out_payload == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto writer = instance->getWriter( to_FlowWriterId( in_writer ) );
        if ( !writer )
        {
            return MXL_ERR_INVALID_FLOW_WRITER;
        }

        auto status = writer->openGrain( in_index, inout_grainInfo, out_payload );
        return status;
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlFlowWriterCancel( mxlInstance in_instance, mxlFlowWriter in_writer )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto writer = instance->getWriter( to_FlowWriterId( in_writer ) );
        if ( !writer )
        {
            return MXL_ERR_INVALID_FLOW_WRITER;
        }

        return writer->cancel();
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlFlowWriterCommit( mxlInstance in_instance, mxlFlowWriter in_writer, const GrainInfo *in_grainInfo )
{
    try
    {
        if ( in_instance == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto writer = instance->getWriter( to_FlowWriterId( in_writer ) );
        if ( !writer )
        {
            return MXL_ERR_INVALID_FLOW_WRITER;
        }

        return writer->commit( in_grainInfo );
    }
    catch ( std::exception & )
    {
        return MXL_ERR_UNKNOWN;
    }
}

mxlStatus
mxlFlowWriterIncrementSlice( mxlInstance in_instance, mxlFlowWriter in_writer, GrainInfo *out_grainInfo )
{
    try
    {
        if ( in_instance == nullptr || out_grainInfo == nullptr )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto instance = to_Instance( in_instance );
        if ( !instance )
        {
            return MXL_ERR_INVALID_ARG;
        }

        auto writer = instance->getWriter( to_FlowWriterId( in_writer ) );
        if ( !writer )
        {
            return MXL_ERR_INVALID_FLOW_WRITER;
        }

        return writer->incrementSlice( out_grainInfo );
    }
    catch ( ... )
    {
        return MXL_ERR_UNKNOWN;
    }
}
