<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Installing MXL System-Wide

```bash
# Build with install target
cmake -B build --preset Linux-GCC-Release
cmake --build build/Linux-GCC-Release --target all

# Install (requires sudo)
sudo cmake --install build/Linux-GCC-Release --prefix /usr/local

# Verify installation
pkg-config --modversion mxl
```

[Back to Building overview](./Building.md)
