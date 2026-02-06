# mxl-info: The Flow Inspector

This tool is your window into the internal workings of MXL flows. Think of it as the diagnostic instrument that reveals what's happening inside the shared-memory domain where MXL flows live.

## The Story

When you're working with MXL flows, you need visibility. Flows exist as directories in a tmpfs domain, but understanding their state, configuration, and health requires more than just listing files. That's where mxl-info comes in. It speaks the language of MXL, translating the internal flow structures into human-readable reports.

## The Implementation: main.cpp

The heart of mxl-info lives in **main.cpp**, a well-structured command-line application that demonstrates proper MXL API usage while providing essential flow management capabilities.

### Core Operations

The tool supports three primary operations, controlled through command-line flags:

**List Mode** (--list or default when no flow specified): Scans the domain directory looking for `.mxl-flow` subdirectories, validates their UUIDs, and retrieves the NMOS flow definition JSON for each. The output is CSV-formatted, showing the flow ID, human-readable label, and NMOS grouphint tag. This gives you a quick inventory of all flows in your domain.

**Inspect Mode** (-f, --flow): When you specify a flow ID, mxl-info creates a flow reader and queries comprehensive information about that specific flow. You'll see the flow format (video, audio, or data), grain or sample rate, batch size hints, runtime state including head index and timestamps, and current latency calculated from the present moment. The latency display even includes color-coding when output to a terminal: green means healthy buffer usage, yellow indicates the buffer is at capacity, and red warns of potential overrun.

**Garbage Collection** (-g, --garbage-collect): Over time, flows can become orphaned when writers or readers crash without proper cleanup. This operation scans for inactive flows (those without active readers or writers) and removes them, reclaiming shared memory resources.

### Architecture Highlights

The code demonstrates several elegant patterns:

**ScopedMxlInstance**: A RAII wrapper class ensures the MXL instance is always properly destroyed, even when exceptions occur. This pattern prevents resource leaks and simplifies error handling throughout the application.

**LatencyPrinter**: This custom formatter wraps mxlFlowInfo and calculates real-time latency by comparing the flow's head index (last committed grain/sample) with the current time index. The wrapper integrates seamlessly with stream operators for clean output formatting.

**URI Support**: Beyond traditional command-line options, mxl-info accepts MXL URIs following the format `mxl:///domain/path?id=flow-uuid`. This forward-looking design anticipates distributed MXL usage where flows might be addressed across network boundaries.

**Terminal Detection**: The tool checks whether stdout is connected to a terminal (using isatty) before applying ANSI color codes. This ensures clean output when piping to files or other programs while providing enhanced readability during interactive use.

### Data Format Awareness

The implementation understands MXL's distinction between discrete and continuous flows. For video and data (discrete formats), it reports grain count and calculates latency in grains. For audio (continuous format), it reports channel count, buffer length, and latency in samples. This awareness permeates the display logic, ensuring the metrics shown are always appropriate for the flow type.

## Command-Line Usage

The tool's interface is intuitive and flexible:

```
# List all flows in a domain
mxl-info -d /tmp/mxl-domain --list

# Inspect a specific flow
mxl-info -d /tmp/mxl-domain -f <flow-uuid>

# Use URI format
mxl-info mxl:///tmp/mxl-domain?id=<flow-uuid>

# Clean up inactive flows
mxl-info -d /tmp/mxl-domain --garbage-collect
```

The tool uses CLI11 for command-line parsing, providing automatic help generation and input validation. The domain path must exist before running, enforced through `CLI::ExistingDirectory` validation.

## What You Learn

Beyond its practical utility, mxl-info serves as a teaching tool. By reading its source, you learn:

- How to properly create and destroy MXL instances
- The correct lifecycle for flow readers
- How to query flow configuration and runtime information
- The structure of NMOS flow definition JSON
- Techniques for calculating and interpreting flow latency
- Proper error handling patterns for MXL operations

This tool embodies the principle that good diagnostic tools are also good documentation. Every line reveals something about how MXL flows work and how applications should interact with them.
