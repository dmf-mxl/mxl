<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: CMake with Presets

This option is for building directly on your host system without containers.

## Install vcpkg

vcpkg is required for dependency management. Follow these steps:

```bash
# Clone vcpkg
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
cd ~/vcpkg

# Bootstrap vcpkg
./bootstrap-vcpkg.sh

# Add vcpkg to your path (add to ~/.bashrc or ~/.zshrc for persistence)
export VCPKG_ROOT=~/vcpkg
export PATH=$VCPKG_ROOT:$PATH
```

## Install apt packages (Ubuntu 22.04/24.04)

```bash
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    ccache \
    doxygen \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad
```

## Build with CMake presets

The list of available presets is:

```
Linux-GCC-Debug
Linux-GCC-Release
Linux-GCC-AddressSanitizer
Linux-GCC-ThreadSanitizer
Linux-GCC-UBSanitizer
Linux-Clang-Debug
Linux-Clang-Release
Linux-Clang-AddressSanitizer
Linux-Clang-UBSanitizer
Darwin-Clang-Debug
Darwin-Clang-Release
Darwin-Clang-AddressSanitizer
Darwin-Clang-UBSanitizer
```

**Build example:**

```bash
# Configure the build
mkdir -p build
cmake -B build --preset Linux-Clang-Debug

# Build everything
cmake --build build/Linux-Clang-Debug --target all
```

**Build specific targets:**

```bash
# Build only the library
cmake --build build/Linux-Clang-Debug --target mxl

# Build only tests
cmake --build build/Linux-Clang-Debug --target tests

# Build only tools
cmake --build build/Linux-Clang-Debug --target mxl-info mxl-gst-testsrc mxl-gst-sink
```

[Back to Building overview](./Building.md)
