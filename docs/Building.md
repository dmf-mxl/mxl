<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building

This document provides comprehensive build instructions for MXL, including prerequisites, build options, troubleshooting, and platform-specific notes.

## Contents

1. [Prerequisites](./Building-Prerequisites.md) - Required tools, libraries, and platform-specific prerequisites (Ubuntu, RHEL, macOS)
2. [Devcontainer Build Environment](./Building-Devcontainer.md) - Using VSCode devcontainers for development (preferred method for WSL2/Linux)
3. [CMake with Presets](./Building-CMake-Presets.md) - Building directly on host system with CMake presets
4. [Running Tests](./Building-Running-Tests.md) - Running all tests, specific tests, verbose output, and parallel execution
5. [Building Rust Crates](./Building-Rust.md) - Installing Rust, building Rust bindings, and GStreamer plugins
6. [Static Build Notes](./Building-Static.md) - Building MXL as static libraries
7. [macOS Notes](./Building-macOS.md) - Platform-specific instructions for macOS
8. [Docker/Container Build Instructions](./Building-Docker.md) - Building MXL in Docker containers
9. [CI/CD Integration](./Building-CI-CD.md) - GitHub Actions and GitLab CI examples
10. [Troubleshooting Common Build Issues](./Building-Troubleshooting.md) - Solutions to common build problems
11. [Using with CMake](./Building-CMake-Integration.md) - Integrating MXL into your CMake projects
12. [Installing MXL System-Wide](./Building-Installation.md) - System-wide installation instructions
13. [Build Performance Tips](./Building-Performance.md) - Optimizing build speed with ccache, Ninja, and parallel builds

## Additional resources

- **CMake documentation:** https://cmake.org/documentation/
- **vcpkg documentation:** https://vcpkg.io/
- **GStreamer build guide:** https://gstreamer.freedesktop.org/documentation/installing/
- **Rust installation guide:** https://www.rust-lang.org/tools/install
