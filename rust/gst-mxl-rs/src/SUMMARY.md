# The GStreamer Plugin: Bridging Two Ecosystems

This folder implements a GStreamer plugin that bridges the worlds of GStreamer pipelines and MXL shared-memory media exchange. It's the story of two elements working in harmony: **mxlsrc** reads from MXL and produces GStreamer buffers, while **mxlsink** receives GStreamer buffers and writes to MXL. Together, they enable zero-copy media sharing between any GStreamer application and any MXL-enabled process.

## The Plugin Foundation

**lib.rs** is the plugin entry point. It defines the `plugin_init` function, which registers both elements with GStreamer, making them available for use in pipelines. The `gst::plugin_define!` macro generates the C-compatible entry point that GStreamer calls when loading the shared library. This macro embeds metadata: plugin name ("mxl"), version (from Cargo with git commit ID), license (Apache-2.0), and description. When GStreamer loads `libgstmxl.so`, it invokes this entry point, which calls `plugin_init`, which registers both `mxlsrc` and `mxlsink`.

## The Sink Element: GStreamer to MXL

The **mxlsink** module receives media buffers from upstream GStreamer elements and writes them to MXL flows for shared-memory distribution.

**mxlsink/mod.rs** defines the public interface. The `MxlSink` wrapper type extends `gst_base::BaseSink`, inheriting GStreamer's sink behavior (synchronization, preroll, state management). The `register` function registers the element with rank NONE (must be explicitly requested, not auto-selected). The module organizes the implementation across several files: `imp` for core logic, `state` for data structures, `render_audio` and `render_video` for format-specific rendering, and `sink_tests` for validation.

**mxlsink/imp.rs** implements the core element behavior. It defines properties (`flow-id`, `domain`), manages state transitions (from NULL to READY to PAUSED to PLAYING), and implements GStreamer trait methods like `start`, `stop`, `set_caps`, and `render`. The `start` method creates an MXL instance and connects to (or creates) the flow. The `set_caps` method negotiates media format (v210 for video, F32LE for audio) and prepares the appropriate writer. The `render` method receives buffers from upstream and dispatches to format-specific rendering functions. State is stored in a `Mutex` to handle GStreamer's threading model.

**mxlsink/state.rs** defines the state structures. `Settings` holds user-configurable properties. `State` holds the runtime context: MXL instance, writer handle (either grain or samples), and flow configuration. The state is protected by a mutex because GStreamer may call methods from different threads.

**mxlsink/render_audio.rs** converts GStreamer audio buffers to MXL samples. It maps the GStreamer buffer (read-only memory), opens an MXL samples write session, copies the audio data channel-by-channel into the MXL ring buffer (handling fragment wrapping), and commits the samples. It synchronizes with the pipeline clock to ensure samples are written at the correct index based on the buffer's presentation timestamp.

**mxlsink/render_video.rs** converts GStreamer video buffers to MXL grains. It maps the buffer, opens an MXL grain write session, copies the video data into the grain payload, and commits all slices. Like audio, it synchronizes with the pipeline clock to write grains at the correct index.

**mxlsink/sink_tests.rs** contains unit and integration tests to validate the sink's behavior across different media formats and pipeline configurations.

## The Source Element: MXL to GStreamer

The **mxlsrc** module reads media from MXL flows and produces GStreamer buffers for downstream elements.

**mxlsrc/mod.rs** defines the public interface. The `MxlSrc` wrapper type extends `gst_base::PushSrc`, which extends `gst_base::BaseSrc` for source elements. PushSrc is designed for sources that produce buffers on-demand via a `create` callback. The `register` function registers the element with rank NONE.

**mxlsrc/imp.rs** implements the core element behavior. It defines properties (`video-flow-id`, `audio-flow-id`, `domain`), manages state transitions, and implements the `create` method that produces buffers. The `start` method creates an MXL instance, connects to the specified flow(s), and negotiates caps based on the flow metadata. The `create` method is called repeatedly by GStreamer's push-based scheduling: it reads from MXL, creates a GStreamer buffer, copies the data, sets timestamps, and returns the buffer. State is protected by a mutex.

**mxlsrc/state.rs** defines the state structures. `Settings` holds properties. `State` holds the MXL instance, reader handles (grain or samples), flow configuration, and timing information (next index to read, base timestamp). The state tracks whether the source is reading video, audio, or both.

**mxlsrc/create_audio.rs** reads audio samples from MXL and produces GStreamer audio buffers. It queries the runtime info to get the head index, calculates the next batch to read, calls `get_samples`, allocates a GStreamer buffer, copies the sample data channel-by-channel (handling fragment wrapping), sets buffer timestamps and duration, and advances the read index.

**mxlsrc/create_video.rs** reads video grains from MXL and produces GStreamer video buffers. It calculates the next grain index based on timing, calls `get_complete_grain` with timeout, allocates a GStreamer buffer, copies the grain payload, sets timestamps and duration, and advances the read index.

**mxlsrc/mxl_helper.rs** provides helper functions for initializing MXL connections and negotiating caps. It queries flow metadata, converts MXL format information to GStreamer caps (e.g., v210 video caps with width/height/framerate, or F32LE audio caps with rate/channels), and sets up the appropriate reader type (grain or samples).

**mxlsrc/src_tests.rs** contains tests to validate the source's behavior, including timing accuracy, caps negotiation, and multi-flow scenarios.

## The Data Flow Architecture

For **mxlsink** (GStreamer to MXL):

```
Upstream element produces buffer
    |
    +--> GStreamer pushes buffer to mxlsink pad
    |
    +--> mxlsink.render() receives buffer
    |       |
    |       +--> Checks buffer timestamp
    |       |
    |       +--> Converts timestamp to MXL index
    |       |
    |       +--> Dispatches to render_audio or render_video
    |               |
    |               +--> Maps GStreamer buffer (read-only)
    |               |
    |               +--> Opens MXL write session (grain or samples)
    |               |
    |               +--> Copies data from GStreamer buffer to MXL ring buffer
    |               |
    |               +--> Commits MXL write session
    |
    +--> Buffer is released (GStreamer ref-counting)
```

For **mxlsrc** (MXL to GStreamer):

```
GStreamer scheduler calls mxlsrc.create()
    |
    +--> Calculates next index to read based on timing
    |
    +--> Dispatches to create_audio or create_video
    |       |
    |       +--> Calls MXL reader (get_grain or get_samples)
    |       |
    |       +--> Allocates GStreamer buffer (writable)
    |       |
    |       +--> Copies data from MXL ring buffer to GStreamer buffer
    |       |
    |       +--> Sets buffer metadata (timestamp, duration)
    |
    +--> Returns buffer to GStreamer
    |
    +--> GStreamer pushes buffer to downstream element
```

## The Threading Model

GStreamer uses a complex threading model:
- **Streaming threads**: Process buffers in the pipeline
- **Application thread**: Sets properties, changes state
- **Clock thread**: Handles synchronization

The plugin handles this by:
- Protecting all mutable state with `Mutex<Option<State>>`
- Using GStreamer's base class infrastructure (`BaseSrc`, `BaseSink`)
- Following GStreamer's state transition rules (NULL -> READY -> PAUSED -> PLAYING)
- Respecting GStreamer's synchronization model (waiting on clock for live sources)

MXL's threading rules are simpler:
- Instances are `Send + Sync` (can be shared across threads)
- Readers and writers are `Send` but not `Sync` (must be used by one thread at a time)

The plugin ensures that each reader/writer is only accessed from the streaming thread, while the instance can be shared across threads.

## The Format Support

Currently supported formats:
- **Video**: v210 (10-bit uncompressed YUV 4:2:2, 32 bytes per 12 pixels)
- **Audio**: F32LE (32-bit floating-point, little-endian, interleaved)

The format negotiation happens in two directions:
- **mxlsink**: Upstream element proposes caps, sink accepts if compatible with MXL
- **mxlsrc**: Source queries MXL flow metadata, generates caps, downstream accepts or rejects

This bidirectional negotiation allows GStreamer pipelines to adapt to MXL flows and vice versa, enabling flexible integration.
