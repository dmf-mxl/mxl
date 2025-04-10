#include "../src/internal/Time.hpp"
#include "Utils.hpp"

#ifndef __APPLE__
#include <Device.h>
#include <Packet.h>
#include <PcapFileDevice.h>
#include <ProtocolType.h>
#include <RawPacket.h>
#include <UdpLayer.h>
#endif

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include <thread>

namespace fs = std::filesystem;

TEST_CASE( "Video Flow : Whole grains", "[mxl flows]" )
{
    const char *opts = "{}";
    auto flowDef = mxl::tests::readFile( "data/v210_flow.json" );
    const char *flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    const char *homeDir = getenv( "HOME" );
    REQUIRE( homeDir != nullptr );
    fs::path domain{ homeDir }; // Remove that path if it exists.
    domain /= "mxl_domain";
    fs::remove_all( domain );

    std::filesystem::create_directories( domain );
    auto instanceReader = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instanceReader != nullptr );

    auto instanceWriter = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instanceWriter != nullptr );

    FlowInfo fInfo;
    REQUIRE( mxlCreateFlow( instanceWriter, flowDef.c_str(), opts, &fInfo ) == MXL_STATUS_OK );

    mxlFlowReader reader;
    REQUIRE( mxlCreateFlowReader( instanceReader, flowId, "", &reader ) == MXL_STATUS_OK );

    mxlFlowWriter writer;
    REQUIRE( mxlCreateFlowWriter( instanceWriter, flowId, "", &writer ) == MXL_STATUS_OK );

    /// Compute the grain index for the flow rate and current TAI time.
    Rational rate{ 60000, 1001 };
    timespec ts;
    mxlGetTime( &ts );
    uint64_t index = mxlTimeSpecToGrainIndex( &rate, &ts );
    REQUIRE( index != MXL_UNDEFINED_OFFSET );

    /// Open the grain.
    GrainInfo gInfo;
    uint8_t *writerBuffer = nullptr;
    /// Open the grain for writing.
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index, &gInfo, &writerBuffer ) == MXL_STATUS_OK );

    FlowInfo fInfo1;
    REQUIRE( mxlFlowReaderGetInfo( instanceReader, reader, &fInfo1 ) == MXL_STATUS_OK );

    /// Set a mark at the beginning and the end of the grain payload.
    writerBuffer[0] = 0xCA;
    writerBuffer[gInfo.grainSize - 1] = 0xFE;

    /// Mark the grain as invalid
    gInfo.flags |= GRAIN_FLAG_INVALID;
    REQUIRE( mxlFlowWriterCommit( instanceWriter, writer, &gInfo ) == MXL_STATUS_OK );

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

    GrainAccessor gAccessor;
    /// Read back the grain using a flow reader.
    REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 16, &gInfo, &gAccessor ) == MXL_STATUS_OK );

    /// Confirm that the flags are preserved.
    REQUIRE( gInfo.flags == GRAIN_FLAG_INVALID );

    /// Confirm that the marks are still present.
    REQUIRE( gAccessor.payload[0] == 0xCA );
    REQUIRE( gAccessor.payload[gInfo.grainSize - 1] == 0xFE );

    /// Get the updated flow info
    FlowInfo fInfo2;
    REQUIRE( mxlFlowReaderGetInfo( instanceReader, reader, &fInfo2 ) == MXL_STATUS_OK );

    /// Confirm that that head has moved.
    REQUIRE( fInfo2.headIndex == index );

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE( fInfo2.lastReadTime > fInfo1.lastReadTime );

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE( fInfo2.lastWriteTime > fInfo1.lastWriteTime );

    /// Delete the reader
    REQUIRE( mxlDestroyFlowReader( instanceReader, reader ) == MXL_STATUS_OK );

    // Use the writer after closing the reader.
    writerBuffer = nullptr;
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index++, &gInfo, &writerBuffer ) == MXL_STATUS_OK );
    /// Set a mark at the beginning and the end of the grain payload.
    writerBuffer[0] = 0xCA;
    writerBuffer[gInfo.grainSize - 1] = 0xFE;

    REQUIRE( mxlDestroyFlowWriter( instanceWriter, writer ) == MXL_STATUS_OK );
    REQUIRE( mxlDestroyFlow( instanceWriter, flowId ) == MXL_STATUS_OK );
    // This should be gone from the filesystem.
    REQUIRE( mxlDestroyFlow( instanceWriter, flowId ) == MXL_ERR_FLOW_NOT_FOUND );

    mxlDestroyInstance( instanceReader );
    mxlDestroyInstance( instanceWriter );
}

TEST_CASE( "Invalid flow definitions", "[mxl flows]" )
{
    const char *opts = "{}";
    auto noGrainRate = mxl::tests::readFile( "data/no_grain_rate.json" );
    auto noId = mxl::tests::readFile( "data/no_id.json" );
    auto noMediaType = mxl::tests::readFile( "data/no_media_type.json" );
    auto malformed = mxl::tests::readFile( "data/malformed.json" );
    auto invalidInterlaced = mxl::tests::readFile( "data/invalid_interlaced_rate.json" );
    auto invalidInterlacedHeight = mxl::tests::readFile( "data/invalid_interlaced_height.json" );

    const char *homeDir = getenv( "HOME" );
    REQUIRE( homeDir != nullptr );
    fs::path domain{ homeDir }; // Remove that path if it exists.
    domain /= "mxl_domain";
    fs::remove_all( domain );

    std::filesystem::create_directories( domain );
    auto instance = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instance != nullptr );

    FlowInfo fInfo;
    REQUIRE( mxlCreateFlow( instance, noGrainRate.c_str(), opts, &fInfo ) != MXL_STATUS_OK );
    REQUIRE( mxlCreateFlow( instance, noId.c_str(), opts, &fInfo ) != MXL_STATUS_OK );
    REQUIRE( mxlCreateFlow( instance, noMediaType.c_str(), opts, &fInfo ) != MXL_STATUS_OK );
    REQUIRE( mxlCreateFlow( instance, malformed.c_str(), opts, &fInfo ) != MXL_STATUS_OK );
    REQUIRE( mxlCreateFlow( instance, invalidInterlaced.c_str(), opts, &fInfo ) != MXL_STATUS_OK );
    REQUIRE( mxlCreateFlow( instance, invalidInterlacedHeight.c_str(), opts, &fInfo ) != MXL_STATUS_OK );

    mxlDestroyInstance( instance );
}

#ifndef __APPLE__

TEST_CASE( "Data Flow : Create/Destroy", "[mxl flows]" )
{
    // Read some 2110-40 packets from a pcap file downloaded from https://github.com/NEOAdvancedTechnology/ST2110_pcap_zoo
    std::unique_ptr<pcpp::IFileReaderDevice> pcapReader( pcpp::IFileReaderDevice::getReader( "data/ST2110-40-Closed_Captions.cap" ) );
    REQUIRE( pcapReader != nullptr );
    REQUIRE( pcapReader->open() );

    // We know that in the pcap file the first packet is an empty packet with a marker bit. Skip it and read the second one.
    pcpp::RawPacketVector rawPackets;
    REQUIRE( pcapReader->getNextPackets( rawPackets, 2 ) == 2 );
    pcpp::Packet parsedPacket( rawPackets.at( 1 ) );

    auto udpLayer = parsedPacket.getLayerOfType<pcpp::UdpLayer>();
    auto *rtpData = udpLayer->getLayerPayload();
    REQUIRE( rtpData != nullptr );
    auto rtpSize = udpLayer->getLayerPayloadSize();
    REQUIRE( rtpSize > 12 );
    rtpData += 12; // skip rtp headers.  hard coded. could be smarter.
    rtpSize -= 12;
    uint8_t ancCount = rtpData[4];
    REQUIRE( ancCount == 1 );

    const char *opts = "{}";
    auto flowDef = mxl::tests::readFile( "data/data_flow.json" );
    const char *flowId = "db3bd465-2772-484f-8fac-830b0471258b";

    const char *homeDir = getenv( "HOME" );
    REQUIRE( homeDir != nullptr );
    fs::path domain{ homeDir }; // Remove that path if it exists.
    domain /= "mxl_domain";
    fs::remove_all( domain );

    std::filesystem::create_directories( domain );
    auto instanceReader = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instanceReader != nullptr );

    auto instanceWriter = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instanceWriter != nullptr );

    FlowInfo fInfo;
    REQUIRE( mxlCreateFlow( instanceWriter, flowDef.c_str(), opts, &fInfo ) == MXL_STATUS_OK );

    mxlFlowReader reader;
    REQUIRE( mxlCreateFlowReader( instanceReader, flowId, "", &reader ) == MXL_STATUS_OK );

    mxlFlowWriter writer;
    REQUIRE( mxlCreateFlowWriter( instanceWriter, flowId, "", &writer ) == MXL_STATUS_OK );

    /// Compute the grain index for the flow rate and current TAI time.
    Rational rate{ 60000, 1001 };
    timespec ts;
    mxlGetTime( &ts );
    uint64_t index = mxlTimeSpecToGrainIndex( &rate, &ts );
    REQUIRE( index != MXL_UNDEFINED_OFFSET );

    /// Open the grain.
    GrainInfo gInfo;
    uint8_t *buffer = nullptr;
    /// Open the grain for writing.
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index, &gInfo, &buffer ) == MXL_STATUS_OK );

    /// ANC Grains are always 4KiB
    REQUIRE( gInfo.grainSize == 4096 );

    // Copy the 2110-40 packet in the grain
    memcpy( buffer, rtpData, rtpSize );

    /// Get some info about the freshly created flow.  Since no grains have been commited, the head should still be at 0.
    FlowInfo fInfo1;
    REQUIRE( mxlFlowReaderGetInfo( instanceReader, reader, &fInfo1 ) == MXL_STATUS_OK );
    REQUIRE( fInfo1.headIndex == 0 );

    /// Mark the grain as invalid
    gInfo.flags |= GRAIN_FLAG_INVALID;
    REQUIRE( mxlFlowWriterCommit( instanceWriter, writer, &gInfo ) == MXL_STATUS_OK );

    /// Create back the grain using a flow reader.
    REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 16, &gInfo, &buffer ) == MXL_STATUS_OK );

    /// Confirm that the flags are preserved.
    REQUIRE( gInfo.flags == GRAIN_FLAG_INVALID );

    /// Confirm that our original -40 packet is still there
    REQUIRE( 0 == memcmp( buffer, rtpData, reinterpret_cast<size_t>( rtpSize ) ) );

    // Give some time to the inotify message to reach the directorywatcher.
    std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );

    /// Get the updated flow info
    FlowInfo fInfo2;
    REQUIRE( mxlFlowReaderGetInfo( instanceReader, reader, &fInfo2 ) == MXL_STATUS_OK );

    /// Confirm that that head has moved.
    REQUIRE( fInfo2.headIndex == index );

    // We accessed the grain using mxlFlowReaderGetGrain. This should have increased the lastReadTime field.
    REQUIRE( fInfo2.lastReadTime > fInfo1.lastReadTime );

    // We commited a new grain. This should have increased the lastWriteTime field.
    REQUIRE( fInfo2.lastWriteTime > fInfo1.lastWriteTime );

    /// Delete the reader
    REQUIRE( mxlDestroyFlowReader( instanceReader, reader ) == MXL_STATUS_OK );

    // Use the writer after closing the reader.
    uint8_t *buffer2 = nullptr;
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index++, &gInfo, &buffer2 ) == MXL_STATUS_OK );

    REQUIRE( mxlDestroyFlowWriter( instanceWriter, writer ) == MXL_STATUS_OK );
    REQUIRE( mxlDestroyFlow( instanceWriter, flowId ) == MXL_STATUS_OK );
    // This should be gone from the filesystem.
    REQUIRE( mxlDestroyFlow( instanceWriter, flowId ) == MXL_ERR_FLOW_NOT_FOUND );

    mxlDestroyInstance( instanceReader );
    mxlDestroyInstance( instanceWriter );
}

#endif

TEST_CASE( "Video Flow : Slices", "[mxl flows]" )
{
    const char *opts = "{}";
    auto flowDef = mxl::tests::readFile( "data/v210_flow.json" );
    const char *flowId = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    const char *homeDir = getenv( "HOME" );
    REQUIRE( homeDir != nullptr );
    fs::path domain{ homeDir }; // Remove that path if it exists.
    domain /= "mxl_domain";
    fs::remove_all( domain );

    std::filesystem::create_directories( domain );
    auto instanceReader = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instanceReader != nullptr );

    auto instanceWriter = mxlCreateInstance( domain.string().c_str(), opts );
    REQUIRE( instanceWriter != nullptr );

    FlowInfo fInfo;
    REQUIRE( mxlCreateFlow( instanceWriter, flowDef.c_str(), opts, &fInfo ) == MXL_STATUS_OK );

    mxlFlowReader reader;
    REQUIRE( mxlCreateFlowReader( instanceReader, flowId, "", &reader ) == MXL_STATUS_OK );

    mxlFlowWriter writer;
    REQUIRE( mxlCreateFlowWriter( instanceWriter, flowId, "", &writer ) == MXL_STATUS_OK );

    /// Compute the grain index for the flow rate and current TAI time.
    Rational rate{ 60000, 1001 };
    timespec ts;
    mxlGetTime( &ts );
    uint64_t index = mxlTimeSpecToGrainIndex( &rate, &ts );
    REQUIRE( index != MXL_UNDEFINED_OFFSET );

    /// Open the grain.
    GrainInfo gInfo;
    gInfo.sliceCount = 16;
    uint8_t *writerBuffer = nullptr;
    /// Open the grain for writing.
    REQUIRE( mxlFlowWriterOpenGrain( instanceWriter, writer, index, &gInfo, &writerBuffer ) == MXL_STATUS_OK );

    // Wait for grain at 'index'.  this should be out of range since we have not written anything yet.
    GrainAccessor gAccessor;
    REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 100, &gInfo, &gAccessor ) == MXL_ERR_OUT_OF_RANGE );

    // Write data one slice at a time
    auto sliceSize = gInfo.grainSize / gInfo.sliceCount;
    for ( auto slice = 0; slice < gInfo.sliceCount; slice++ )
    {
        // Byte offset of the beginning of the slice
        auto offset = slice * sliceSize;
        // Set markers at the beginning and end of the slice with the slice number
        writerBuffer[offset] = slice;
        writerBuffer[offset + sliceSize - 1] = slice;
        // Increment grain slice
        mxlFlowWriterIncrementSlice( instanceWriter, writer, &gInfo );
        // Read back the grain and confirm that the slice information is properly updated.
        GrainAccessor gAccessor;
        REQUIRE( mxlFlowReaderGetGrain( instanceReader, reader, index, 100, &gInfo, &gAccessor ) == MXL_STATUS_OK );
        // Verify that the amount of valid bytes is correct
        REQUIRE( gAccessor.validSize == sliceSize * ( slice + 1 ) );
        // Verify that the total payload size is correct.
        REQUIRE( gAccessor.payloadSize == gInfo.grainSize );
        // Verify that the payload marker is also correct.
        REQUIRE( gAccessor.payload[offset] == gInfo.validSliceCount - 1 );
        REQUIRE( gAccessor.payload[offset + sliceSize - 1] == gInfo.validSliceCount - 1 );
    }

    REQUIRE( mxlDestroyFlowReader( instanceReader, reader ) == MXL_STATUS_OK );
    REQUIRE( mxlDestroyFlowWriter( instanceWriter, writer ) == MXL_STATUS_OK );
    REQUIRE( mxlDestroyFlow( instanceWriter, flowId ) == MXL_STATUS_OK );

    mxlDestroyInstance( instanceReader );
    mxlDestroyInstance( instanceWriter );
}