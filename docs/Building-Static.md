<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Static Build Notes

By default, the CMake presets build MXL as shared libraries. To build MXL as static libraries instead, use the CMake option `-DBUILD_SHARED_LIBS=OFF`.

**Example:**

```bash
cmake -B build --preset Linux-GCC-Debug -DBUILD_SHARED_LIBS=OFF
cmake --build build/Linux-GCC-Debug --target all
```

PIC is enabled by default and can be disabled with `-DMXL_ENABLE_PIC=OFF`:

```bash
cmake -B build --preset Linux-GCC-Debug -DBUILD_SHARED_LIBS=OFF -DMXL_ENABLE_PIC=OFF
cmake --build build/Linux-GCC-Debug --target all
```

[Back to Building overview](./Building.md)
