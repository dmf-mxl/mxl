<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: macOS Notes

## Install prerequisites

1. **Install Homebrew**
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **Install build tools**
   ```bash
   brew install cmake ccache doxygen
   ```

3. **Install GStreamer**
   - Download and install the GStreamer runtime and development packages from:
     https://gstreamer.freedesktop.org/documentation/installing/on-mac-osx.html

## Build on macOS

```bash
# Configure
cmake -B build --preset Darwin-Clang-Debug

# Build
cmake --build build/Darwin-Clang-Debug --target all

# Run tests
cd build/Darwin-Clang-Debug
ctest --output-on-failure
```

[Back to Building overview](./Building.md)
