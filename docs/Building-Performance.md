<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Build Performance Tips

1. **Use ccache to speed up recompilation:**
   ```bash
   # Install ccache
   sudo apt-get install ccache

   # Configure CMake to use ccache
   cmake -B build --preset Linux-GCC-Debug \
       -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
   ```

2. **Use Ninja for faster builds:**
   ```bash
   # Install Ninja
   sudo apt-get install ninja-build

   # Configure with Ninja
   cmake -B build --preset Linux-GCC-Debug -G Ninja

   # Build
   ninja -C build/Linux-GCC-Debug
   ```

3. **Parallel builds:**
   ```bash
   # Use all available cores
   cmake --build build/Linux-GCC-Debug --target all -j$(nproc)
   ```

4. **Incremental builds:**
   ```bash
   # Only rebuild changed targets
   cmake --build build/Linux-GCC-Debug --target mxl
   ```

[Back to Building overview](./Building.md)
