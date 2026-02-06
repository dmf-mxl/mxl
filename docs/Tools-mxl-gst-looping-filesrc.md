<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools: mxl-gst-looping-filesrc

A utility that continuously loops through MPEG-TS files to generate video grains to push into MXL flows.

## When would I use this tool?

- **Regression testing:** Loop a reference file for continuous testing
- **Load testing:** Generate sustained load from known content
- **Demo/trade shows:** Loop promotional content indefinitely

## Prerequisites

This application requires the `looping_filesrc` GStreamer plugin located in `utils/gst-looping-filesrc/`. After building the plugin successfully, set the GST_PLUGIN_PATH environment variable to enable GStreamer to locate and load the plugin.

```bash
export GST_PLUGIN_PATH="./build/Linux-GCC-Release/utils/gst-looping-filesrc:${GST_PLUGIN_PATH}"
```

**Verify Installation:**

```bash
gst-inspect-1.0 looping_filesrc
```

## Usage

```bash
./mxl-gst-looping-filesrc [OPTIONS]

OPTIONS:
  -h,     --help              Print this help message and exit
  -d,     --domain TEXT REQUIRED
                              The MXL domain directory
  -i,     --input TEXT:FILE REQUIRED
                              MPEGTS media file location
```

## Example

```bash
./mxl-gst-looping-filesrc \
  -d /dev/shm/mxl \
  -i /path/to/test-content.ts
```

## Troubleshooting

**Problem: "looping_filesrc plugin not found"**

Solution: Build and register the plugin.

```bash
cd utils/gst-looping-filesrc
make
export GST_PLUGIN_PATH=$(pwd):$GST_PLUGIN_PATH
gst-inspect-1.0 looping_filesrc  # Should show plugin details
```

**Problem: "File not recognized as MPEG-TS"**

Solution: Verify file format.

```bash
gst-typefind-1.0 /path/to/file.ts
# Should show: video/mpegts
```

[Back to Tools overview](./Tools.md)
