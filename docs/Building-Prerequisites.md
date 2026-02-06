<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Prerequisites

Before building MXL, ensure you have the following installed:

## Required tools

- **CMake** 3.20 or later
- **C++ compiler** with C++17 support:
  - GCC 9+ (Linux)
  - Clang 10+ (Linux/macOS)
  - MSVC 19.20+ (Windows, experimental)
- **vcpkg** (for dependency management)
- **Git** (for cloning the repository)

## Required libraries

The following dependencies are managed automatically via vcpkg:

- **spdlog** (logging)
- **nlohmann-json** (JSON parsing)
- **CLI11** (command-line parsing for tools)
- **Catch2** (unit testing)
- **GStreamer** 1.0+ (optional, for GStreamer tools)

## Ubuntu/Debian prerequisites

```bash
# Install build essentials
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    ccache \
    doxygen
```

## RHEL/CentOS/Fedora prerequisites

```bash
# Install build essentials
sudo dnf install -y \
    gcc \
    gcc-c++ \
    cmake \
    git \
    pkg-config \
    ccache \
    doxygen
```

## macOS prerequisites

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install build tools
brew install cmake ccache doxygen
```

[Back to Building overview](./Building.md)
