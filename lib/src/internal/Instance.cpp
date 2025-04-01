#include "Instance.hpp"

#include "DomainWatcher.hpp"
#include "FlowManager.hpp"
#include "FlowParser.hpp"
#include "FlowReader.hpp"
#include "FlowWriter.hpp"
#include "Logging.hpp"
#include "PosixFlowReader.hpp"
#include "PosixFlowWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <picojson/picojson.h>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <uuid.h>

namespace mxl::lib {

namespace {

std::once_flag loggingFlag;

void
initializeLogging()
{
    auto console = spdlog::stdout_color_mt( "console" );
    spdlog::set_default_logger( console );
    spdlog::cfg::load_env_levels( "MXL_LOG_LEVEL" );
}

} // namespace

Instance::Instance( const std::filesystem::path &in_mxlDomain, const std::string &in_options ) : _options( in_options )
{
    std::call_once( loggingFlag, [&]() { initializeLogging(); } );
    _flowManager = std::make_shared<FlowManager>( in_mxlDomain );
    _watcher =
        std::make_shared<DomainWatcher>( in_mxlDomain, [this]( const auto &in_uuid, auto in_type ) { fileChangedCallback( in_uuid, in_type ); } );
    MXL_DEBUG( "Instance created. MXL Domain: {}", in_mxlDomain.string() );
}

Instance::~Instance()
{
    _stopping = true;
    _watcher->stop();
    std::lock_guard<std::mutex> lock( _mutex );
    _readers.clear();
    _readersByUUID.clear();
    _writers.clear();
    _watcher.reset();
    MXL_DEBUG( "Instance destroyed." );
    spdlog::default_logger()->flush();
}

void
Instance::fileChangedCallback( const uuids::uuid &in_flowId, WatcherType in_type )
{
    if ( !_stopping )
    {
        std::lock_guard<std::mutex> lock( _mutex );

        // Someone wrote to the flow. let the readers know.
        if ( in_type == WatcherType::READER )
        {
            auto found = _readersByUUID.find( in_flowId );
            if ( found != _readersByUUID.end() )
            {
                found->second->grainAvailable();
            }
        }
        else
        {
            // Someone read the grain and touched the ".access" file.  let update the last read count.
            auto found = _writersByUUID.find( in_flowId );
            if ( found != _writersByUUID.end() )
            {
                found->second->flowRead();
            }
        }
    }
}

FlowReaderId
Instance::createFlowReader( const std::string &in_flowId )
{
    auto id = uuids::uuid::from_string( in_flowId );
    auto reader = std::make_shared<PosixFlowReader>( _flowManager );
    if ( !reader->open( *id ) )
    {
        throw std::runtime_error( "Failed to open flow " + in_flowId );
    }

    _watcher->addFlow( *id, WatcherType::READER );

    std::lock_guard<std::mutex> lock( _mutex );
    const FlowReaderId index{ _readerCounter++ };
    _readers[index] = reader;
    _readersByUUID[*id] = reader;
    return index;
}

std::shared_ptr<FlowReader>
Instance::getReader( FlowReaderId in_id )
{
    std::lock_guard<std::mutex> lock( _mutex );
    auto found = _readers.find( in_id );
    if ( found != _readers.end() )
    {
        return found->second;
    }
    else
    {
        return nullptr;
    }
}

bool
Instance::removeReader( FlowReaderId in_id )
{
    std::lock_guard<std::mutex> lock( _mutex );
    auto found = _readers.find( in_id );
    if ( found != _readers.end() )
    {
        auto reader = ( *found ).second;
        auto flowId = reader->getId();

        _watcher->removeFlow( ( *found ).second->getId(), WatcherType::READER );
        ( *found ).second->close();
        _readers.erase( found );

        auto byId = _readersByUUID.find( flowId );
        if ( byId != _readersByUUID.end() )
        {
            _readersByUUID.erase( byId );
        }
        else
        {
            MXL_WARN( "This should not happen." );
        }

        return true;
    }
    else
    {
        return false;
    }
}

FlowWriterId
Instance::createFlowWriter( const std::string &in_flowId )
{
    auto id = uuids::uuid::from_string( in_flowId );
    auto writer = std::make_shared<PosixFlowWriter>( _flowManager );
    writer->open( *id );

    _watcher->addFlow( *id, WatcherType::WRITER );

    std::lock_guard<std::mutex> lock( _mutex );
    const FlowWriterId index{ _writerCounter++ };
    _writers[index] = writer;
    _writersByUUID[*id] = writer;
    return index;
}

std::shared_ptr<FlowWriter>
Instance::getWriter( FlowWriterId in_id )
{
    std::lock_guard<std::mutex> lock( _mutex );
    auto found = _writers.find( in_id );
    if ( found != _writers.end() )
    {
        return found->second;
    }
    else
    {
        return nullptr;
    }
}

bool
Instance::removeWriter( FlowWriterId in_id )
{
    std::lock_guard<std::mutex> lock( _mutex );
    auto found = _writers.find( in_id );
    if ( found != _writers.end() )
    {
        _watcher->removeFlow( found->second->getId(), WatcherType::WRITER );

        ( *found ).second->close();
        _writers.erase( found );
        return true;
    }
    else
    {
        return false;
    }
}

FlowData::ptr
Instance::createFlow( const std::string &in_flowDef )
{
    // Parse the json flow resource
    FlowParser parser{ in_flowDef };
    // Read the mandatory grain_rate field
    Rational grainRate = parser.getGrainRate();
    // Read the mandatory id field
    auto id = parser.getId();
    // Compute the grain count based on our configured history duration
    size_t grainCount = _historyDuration / ( 1000 * grainRate.denominator / grainRate.numerator );

    // Create the flow using the flow manager
    auto flowData = _flowManager->createFlow( id, in_flowDef, grainCount, parser.getPayloadSize() );
    auto info = flowData->flow->get()->info;

    // Populate the flowinfo structure. This structure will be visible through the C api.
    info.version = 1;
    info.size = sizeof( FlowInfo );
    auto idSpan = id.as_bytes();
    std::memcpy( info.id, idSpan.data(), idSpan.size() );
    info.flags = 0;
    info.headIndex = 0;
    info.grainRate = grainRate;
    info.grainCount = grainCount;

    mxlGetTime( &info.lastWriteTime );
    mxlGetTime( &info.lastReadTime );

    return flowData;
}

bool
Instance::deleteFlow( const uuids::uuid &in_id )
{
    return _flowManager->deleteFlow( in_id, nullptr );
}

Instance *
to_Instance( mxlInstance in_instance )
{
    return (in_instance != nullptr)
        ? reinterpret_cast<InstanceInternal *>( in_instance )->instance.get()
        : nullptr;
}

FlowReaderId
to_FlowReaderId( mxlFlowReader in_reader )
{
    return static_cast<FlowReaderId>( reinterpret_cast<std::uintptr_t>( in_reader ) );
}

FlowWriterId
to_FlowWriterId( mxlFlowWriter in_writer )
{
    return static_cast<FlowWriterId>( reinterpret_cast<std::uintptr_t>( in_writer ) );
}

}
