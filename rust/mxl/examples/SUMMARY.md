# The Example Programs: Learning Through Practice

This folder contains example programs that demonstrate how to use the MXL Rust API in real-world scenarios. They're not just toy examples; they're complete, working applications that read and write media data with proper timing, synchronization, and error handling. They serve as templates for building MXL-enabled applications.

## The Shared Foundation

**common/mod.rs** provides utilities shared across all examples. The `setup_logging` function configures the tracing subscriber to output logs at INFO level (or whatever `RUST_LOG` environment variable specifies) to stdout. This is a standard pattern in Rust: create a shared module for common functionality, keeping the main example files focused on the core logic.

## The Writer Example: Producing Media

**flow-writer.rs** demonstrates how to create an MXL flow and continuously write media data to it. The program accepts command-line arguments via `clap`:
- `--mxl-domain`: The shared memory path (e.g., `/dev/shm/my_domain`)
- `--flow-config-file`: Path to a JSON flow definition file
- `--grain-or-sample-count`: Optional limit on how many grains or samples to write
- `--sample-batch-size`: For audio flows, the batch size (defaults to ~10ms)

The `main` function orchestrates the process:
1. Loads the MXL API from the shared library
2. Creates an MXL instance connected to the domain
3. Reads the flow definition from the JSON file
4. Creates a flow writer (which either creates a new flow or reuses an existing one)
5. Checks if the flow is discrete (grain-based) or continuous (sample-based)
6. Dispatches to the appropriate writer function

The **write_grains** function demonstrates discrete media writing:
- Queries the grain rate (e.g., 60/1 for 60 fps)
- Gets the current index using `get_current_index`
- Enters a loop that:
  - Opens a grain at the current index
  - Fills the grain payload with a test pattern (byte value depends on position and index)
  - Commits all slices
  - Calculates when the next grain should be written using `index_to_timestamp`
  - Sleeps until that time using `sleep_for`
  - Increments the index
- Explicitly destroys the writer when done (which deletes the flow)

The **write_samples** function demonstrates continuous audio writing:
- Queries the sample rate (e.g., 48000/1 for 48 kHz)
- Determines the batch size (from args, writer hint, or ~10ms default)
- Gets the current index
- Enters a loop that:
  - Opens a samples write session for the batch
  - Fills each channel with a test pattern (handling fragment wrapping)
  - Commits the samples
  - Calculates when the next batch should be written
  - Sleeps until that time
  - Advances the index by the batch size
- Explicitly destroys the writer when done

The key lesson: timing matters. The writer synchronizes with real-time by calculating timestamps and sleeping between writes. This prevents the writer from running ahead and overwriting data that hasn't been read yet.

## The Reader Example: Consuming Media

**flow-reader.rs** demonstrates how to connect to an existing MXL flow and continuously read media data from it. The program accepts command-line arguments:
- `--mxl-domain`: The shared memory path
- `--flow-id`: The UUID of the flow to read from
- `--sample-batch-size`: For audio flows, the batch size (defaults to writer's hint or ~10ms)

The `main` function orchestrates:
1. Loads the MXL API
2. Creates an MXL instance
3. Creates a flow reader for the specified flow ID
4. Queries flow info to determine the type
5. Dispatches to the appropriate reader function

The **read_grains** function demonstrates discrete media reading:
- Queries the grain rate
- Gets the current index (starting point)
- Enters an infinite loop that:
  - Calls `get_complete_grain` with a 5-second timeout
  - Waits for the grain to be completely written (all slices valid)
  - Logs the grain size
  - Increments the index

The blocking read with timeout ensures the reader waits for new data rather than failing immediately if the writer is slow.

The **read_samples** function demonstrates continuous audio reading:
- Queries the sample rate
- Determines the batch size (from args, writer hint, or default)
- Gets the current head index from runtime info (where to start reading)
- Tracks when the next batch should be ready using `read_head_valid_at`
- Enters an infinite loop that:
  - Calls `get_samples_non_blocking` to fetch the batch
  - Logs the channel count and buffer sizes (including fragment wrapping)
  - Calculates when the next batch should be ready
  - Sleeps in smaller increments until that time
  - Checks if data is available (polls `headIndex`)
  - Times out after 5 seconds if no data arrives
  - Advances the index by the batch size

The key lesson: readers must synchronize with writers. For samples, the reader manually tracks timing and polls for data availability, implementing its own timeout logic. (The code includes a TODO comment noting that a newer MXL blocking read API could simplify this.)

## The Learning Path

These examples demonstrate:

1. **API initialization**: Load library, create instance, connect to domain
2. **Flow creation**: Parse JSON definitions, create flows with metadata
3. **Reader creation**: Connect to existing flows by UUID
4. **Type dispatch**: Check flow type and convert to typed reader/writer
5. **Zero-copy access**: Open sessions, get mutable/immutable slices, work with raw bytes
6. **RAII pattern**: Commit or cancel sessions, automatic cleanup on drop
7. **Timing synchronization**: Convert between indices and timestamps, sleep with precision
8. **Error handling**: Use `?` operator for propagation, handle specific error types
9. **Resource cleanup**: Explicitly destroy writers to delete flows (or rely on drop)
10. **Command-line parsing**: Use `clap` for args, derive parsers from structs

The examples are production-quality: they handle errors, log useful information, accept configuration via arguments, and clean up resources properly. They're ready to be adapted for real applications.

## Running the Examples

To run the writer:
```bash
cargo run --example flow-writer -- \
    --mxl-domain /dev/shm/my_domain \
    --flow-config-file examples/v210_flow.json \
    --grain-or-sample-count 100
```

To run the reader (in another terminal):
```bash
cargo run --example flow-reader -- \
    --mxl-domain /dev/shm/my_domain \
    --flow-id <uuid-from-json-file>
```

The reader will continuously display grain/sample information until stopped with Ctrl-C. The writer will run for the specified count and then clean up the flow.

These examples are the starting point for anyone building MXL applications: copy them, modify them, and build on them. They demonstrate the patterns and practices that make MXL integration successful.
