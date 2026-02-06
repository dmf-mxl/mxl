<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Usage: Rust API Examples

MXL provides Rust bindings with idiomatic Rust APIs. See `rust/mxl-rs/README.md` for detailed documentation.

## Basic usage in Rust

```rust
use mxl_rs::{Instance, FlowWriter, FlowReader};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create instance
    let instance = Instance::new("/dev/shm/mxl", None)?;

    // Create writer
    let flow_def = r#"{
        "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ed",
        "format": "urn:x-nmos:format:video",
        "media_type": "video/v210",
        "grain_rate": {"numerator": 30000, "denominator": 1001},
        "frame_width": 1920,
        "frame_height": 1080
    }"#;

    let (writer, was_created) = FlowWriter::create(&instance, flow_def, None)?;
    println!("Flow created: {}", was_created);

    // Write a grain
    let grain_index = 0;
    let mut grain = writer.open_grain(grain_index)?;

    // Fill grain buffer
    let buffer = grain.buffer_mut();
    buffer.fill(0x80);  // Fill with grey

    grain.set_origin_timestamp(12345678900);
    grain.commit()?;

    // Create reader
    let reader = FlowReader::open(&instance, "5fbec3b1-1b0f-417d-9059-8b94a47197ed", None)?;

    // Read grain
    let read_grain = reader.get_grain(grain_index, std::time::Duration::from_millis(100))?;
    println!("Read grain of size {} bytes", read_grain.buffer().len());

    Ok(())
}
```

## Key features

The Rust API provides:

- RAII-based resource management (automatic cleanup)
- Type-safe enums for formats and status codes
- Iterator-based APIs for batch processing
- Zero-copy buffer access with safe Rust slices

---

Back to [Usage overview](./Usage.md)
