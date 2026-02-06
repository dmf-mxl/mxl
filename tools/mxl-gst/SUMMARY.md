# mxl-gst: GStreamer Integration Tools

This collection bridges the world of GStreamer multimedia processing with MXL's shared-memory flow architecture. Together, these three tools demonstrate how MXL integrates with real-world media pipelines, handling everything from synthetic test patterns to file playback.

## The Foundation: utils.hpp

Before diving into the individual tools, let's examine **utils.hpp**, the shared utility library that all three tools depend on. This header provides the common infrastructure that makes the tools consistent and maintainable.

### Logging Macros

The file defines a unified logging system through five macros: MXL_ERROR, MXL_WARN, MXL_INFO, MXL_DEBUG, and MXL_TRACE. All output to stderr with standardized formatting and support fmt::format style formatting strings. This consistency makes debugging across tools straightforward, as all messages follow the same structure.

### Media Format Utilities

The `media_utils` namespace contains specialized functions for video buffer calculations. The star here is `getV210LineLength()`, which calculates the byte length of a v210 video line. V210 is a 10-bit YUV 4:2:2 packed format commonly used for uncompressed video, where pixels pack into 32-bit words in groups of 6 pixels (128 bits per 6 pixels). This function handles the necessary alignment to 48-pixel boundaries.

### JSON Parsing Utilities

The `json_utils` namespace provides tools for working with NMOS flow definitions. Functions like `parseFile()` and `parseBuffer()` handle JSON parsing with proper error handling. `getField()` and `getFieldOr()` extract typed values with optional defaults. The `getRational()` function specifically handles NMOS rational values (numerator/denominator pairs) used for frame rates and sample rates. Finally, `updateGroupHint()` and `serializeJson()` allow manipulation of flow metadata, particularly the NMOS grouphint tag that logically groups related flows.

## Tool 1: testsrc.cpp - The Test Pattern Generator

**testsrc.cpp** generates synthetic media and writes it into MXL flows. This is your go-to tool when you need a reliable, controllable media source for testing.

### The Video Pipeline

The tool constructs a GStreamer pipeline that generates v210 video test patterns. The pipeline flows: videotestsrc → textoverlay → clockoverlay → videoconvert → appsink. You control the pattern type (SMPTE bars, snow, solid colors, checkers, zone plates, etc.), frame dimensions, frame rate, and overlay text. The code defines 25 different patterns in the `pattern_map`, from classic SMPTE bars to more exotic patterns like pinwheels and chroma zone plates.

### The Audio Pipeline

Audio generation creates multi-channel test tones using separate audiotestsrc elements per channel, interleaved into float32 non-interleaved format at 48kHz. Each channel gets a different frequency (100Hz increments starting at 100Hz), making it easy to verify channel routing. The pipeline structure: multiple audiotestsrc elements → interleave → audioconvert → appsink.

### Sub-Grain Commits

One of the most educational aspects of testsrc is its demonstration of sub-grain synchronization. Video frames can be committed in progressive slices rather than all at once, simulating real-world cameras that produce progressive JPEG scanlines. The tool calculates how many batches are needed based on the `maxCommitBatchSizeHint`, spreads them across half a frame period, and commits each slice with an updated `validSlices` count. This shows readers exactly how much of a grain is ready for consumption before the entire frame arrives.

### Skipped Frame Handling

When GStreamer misses frames (comparing expected grain index to actual buffer PTS), the tool generates invalid grains marked with `MXL_GRAIN_FLAG_INVALID`. This maintains timing continuity, preventing readers from blocking indefinitely on missing data. The same pattern applies to audio, where silence is inserted (with a production note that real applications should apply micro-fades to prevent clicks).

### TAI Timestamp Alignment

The tool goes to great lengths to align GStreamer's clock with MXL's TAI-based timing. It creates a GstSystemClock configured for GST_CLOCK_TYPE_TAI, sets the pipeline base time to the next epoch grain boundary, and converts all buffer PTS values to TAI absolute timestamps before indexing into the MXL flow.

## Tool 2: sink.cpp - The Flow Reader and Player

**sink.cpp** is the complement to testsrc. It reads from MXL flows and plays the media through GStreamer's auto-sink elements (autovideosink and autoaudiosink).

### Zero-Copy Architecture

The tool demonstrates efficient data handling by mapping MXL payloads and copying them into GStreamer buffers only once. For video, it creates a GstBuffer, maps it for writing, copies the grain payload, and pushes it to the appsrc element. Audio follows a similar pattern but handles the multi-channel, non-interleaved layout by iterating through fragments and channels.

### The Cursor Pattern

A sophisticated timing mechanism called `Cursor` manages read indexing. The cursor tracks three key values: the current index (aligned to present time), the requested index (offset into the past by readDelay), and the delivery deadline (when the next grain must be available to GStreamer). This pattern ensures smooth playback despite variable processing latencies.

### Read Delay and Buffering

The `--read-delay` parameter controls how far in the past to read. A positive value (default 40ms) provides buffering headroom, reducing the risk of underruns. The tool aligns reads to window boundaries (grain or sample batch sizes), ensuring consistent chunk sizes for GStreamer.

### A/V Synchronization

The `--av-delay` parameter allows fine-tuning audio-video sync. Positive values delay audio relative to video, negative values do the opposite. The `--playback-delay` applies a global offset to both. These separate controls provide flexibility for compensating system-specific latencies.

### Error Handling and Reconnection

The code handles multiple error scenarios: MXL_ERR_OUT_OF_RANGE_TOO_EARLY (not ready yet, retry), MXL_ERR_OUT_OF_RANGE_TOO_LATE (data expired, realign to current time), and MXL_ERR_FLOW_INVALID (flow recreated, reconnect). The reconnection logic releases the old reader, creates a new one, and realigns the cursor, allowing seamless recovery from upstream restarts.

### Latency Tracking

The tool tracks and reports the highest latency encountered during operation. This metric reveals whether your read delay provides sufficient margin or if you're experiencing near-miss conditions.

## Tool 3: looping_filesrc.cpp - The File Player

**looping_filesrc.cpp** bridges file-based media and MXL flows. Point it at any media file, and it decodes the content into MXL flows for consumption by other applications.

### Dynamic Pipeline Construction

The tool uses GStreamer's decodebin element, which automatically handles format detection and codec selection. As decodebin discovers pads (video and audio streams), the `cb_pad_added` callback dynamically constructs appropriate sub-pipelines:

For video: queue → videorate → videoconvert → appsink (v210, bt709 colorimetry)
For audio: queue → audioconvert → appsink (F32LE, 48kHz, non-interleaved)

This dynamic approach supports any container format GStreamer can decode: MP4, MPEG-TS, MKV, AVI, MOV, etc.

### Flow Creation On-the-Fly

Unlike testsrc, which requires pre-existing NMOS flow descriptors, looping_filesrc generates them automatically. Once the pipeline negotiates capabilities, the tool extracts width, height, frame rate, channel count, and sample rate from the GStreamer caps. Functions `createVideoFlowJson()` and `createAudioFlowJson()` build NMOS-compliant flow descriptors with appropriate UUIDs, component definitions, and metadata.

### The Thread Architecture

The tool spawns two threads: `videoThread()` and `audioThread()`, which independently pull samples from their respective appsinks and write to MXL flows. This parallelism ensures that audio doesn't block on video processing (or vice versa), maintaining smooth playback timing for both streams.

### Clock Offset Compensation

GStreamer's buffer timestamps don't immediately align with MXL's TAI epoch. The tool calculates offsets (`videoAppSinkOffset` and `audioAppSinkOffset`) on the first buffer, storing the difference between MXL time and the adjusted GStreamer PTS. All subsequent buffers apply this offset, ensuring consistent MXL indexing throughout the file playback.

### Graceful Degradation

Like testsrc, this tool handles frame and sample skips by generating invalid grains or silence. The logic compares expected indices with actual buffer timestamps, filling gaps to maintain flow continuity. The tool even provides helpful notes in the code about production considerations, like applying fades when inserting silence to prevent audio artifacts.

## Command-Line Usage

Each tool provides a comprehensive command-line interface:

```
# Generate test patterns
mxl-gst-testsrc -d /tmp/domain \
  -v video_flow.json -a audio_flow.json \
  --pattern smpte --overlay-text "Test Source"

# Play from MXL flows
mxl-gst-sink -d /tmp/domain \
  -v <video-flow-id> -a <audio-flow-id> \
  --read-delay 40000000 --av-delay 0

# Play a media file into MXL
mxl-gst-looping-filesrc -d /tmp/domain \
  -i /path/to/media.ts
```

All three tools use CLI11 for argument parsing, providing automatic validation, help generation, and consistent error messages.

## What These Tools Teach

Together, the mxl-gst suite demonstrates:

- Integration patterns between GStreamer and MXL
- Proper handling of TAI timestamps and clock synchronization
- Techniques for sub-grain/sub-sample synchronization
- Buffer management for zero-copy operation
- Error handling and recovery strategies
- The distinction between discrete (video) and continuous (audio) flow handling
- Dynamic pipeline construction and capability negotiation
- Multi-threaded media processing patterns

These aren't just demo applications. They're production-quality templates showing how to build real media applications on top of MXL. The patterns and techniques here translate directly to custom applications, whether you're building a live camera interface, a file transcoder, or a network media gateway.
