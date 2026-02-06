<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Building Rust Crates

MXL includes Rust components (Rust bindings and GStreamer plugins). To build them:

## Install Rust

```bash
# Install rustup (Rust toolchain manager)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Restart shell or source the environment
source $HOME/.cargo/env
```

## Build Rust components

```bash
# Build Rust bindings
cd rust/mxl-rs
cargo build --release

# Build GStreamer plugin
cd ../gst-mxl-rs
cargo build --release

# Run Rust tests
cargo test
```

## Install GStreamer plugin

```bash
# Copy plugin to system location (optional)
sudo cp target/release/libgstmxl.so /usr/lib/x86_64-linux-gnu/gstreamer-1.0/

# Or set GST_PLUGIN_PATH for development
export GST_PLUGIN_PATH=$(pwd)/target/release:$GST_PLUGIN_PATH

# Verify plugin installation
gst-inspect-1.0 mxlsrc
gst-inspect-1.0 mxlsink
```

[Back to Building overview](./Building.md)
