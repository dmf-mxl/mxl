# The FFI Bindings: Where C Meets Rust

This folder is the boundary between two worlds: the C implementation of MXL and the safe Rust wrapper. It's where bindgen transforms C headers into Rust declarations, where CMake builds are orchestrated, and where raw, unsafe function pointers become callable Rust functions.

## The Library Interface

**lib.rs** is deceptively simple, but it's the culmination of a complex build process. It includes the bindgen-generated `bindings.rs` file from the build output directory and configures compiler warnings to suppress the expected noise from auto-generated code. The file explicitly allows non-standard naming conventions (`non_upper_case_globals`, `non_camel_case_types`, `non_snake_case`) because bindgen preserves C naming patterns. It also allows missing docs, broken intra-doc links, and unsafe operations because this is low-level FFI code where such things are unavoidable.

The included `bindings.rs` (generated at build time) contains:
- Raw C types: `Instance`, `FlowReader`, `FlowWriter`, `FlowConfigInfo`, `FlowRuntimeInfo`, `GrainInfo`, etc.
- Raw C functions: transformed by the custom callback from `mxlCreateInstance` to `create_instance`, `mxlGetCurrentIndex` to `get_current_index`, etc.
- Constants: status codes like `MXL_STATUS_OK`, `MXL_ERR_TIMEOUT`, `MXL_ERR_FLOW_NOT_FOUND`, and data format constants like `MXL_DATA_FORMAT_VIDEO`, `MXL_DATA_FORMAT_AUDIO`
- A dynamically-loaded function table (via `libloading`) that can be instantiated at runtime by pointing to a shared library file

This crate exposes everything as `unsafe` because it's a direct, unverified bridge to C code. The safe wrapper (`mxl`) is responsible for upholding all invariants.

## The Build System Bridge

**build.rs** is where the magic happens. This build script runs at compile time and performs three critical tasks:

1. **Optionally Build MXL from Source**

   If the `mxl-not-built` feature is NOT enabled, the script invokes CMake to build the entire MXL C library from source. It configures the build with:
   - Generator: Ninja (fast parallel builds)
   - Preset: `Linux-Clang-Debug` or `Linux-Clang-Release` depending on Rust's build mode
   - Output directory: Cargo's `OUT_DIR` (the build output folder)
   - Build flags: `BUILD_DOCS=OFF`, `BUILD_TESTS=OFF`, `BUILD_TOOLS=OFF` (only build the library)

   The script then tells Cargo where to find the compiled library using `cargo:rustc-link-search` and `cargo:rustc-link-lib=mxl`. It also sets up rebuild triggers so that changes to the `lib/` directory cause a recompile.

   This is powerful: Rust can build and link C code automatically, making the entire MXL library available as a Rust dependency without any manual build steps.

2. **Configure Bindgen**

   The `get_bindgen_specs` function determines which header file to use:
   - `wrapper-with-version-h.h`: When building from source (includes version metadata)
   - `wrapper-without-version-h.h`: When using a pre-built library

   It sets up include directories pointing to `lib/include` (the C headers) and, if building from source, the build output directory (for generated version headers). These directories are also emitted as `cargo:include=` metadata for downstream crates.

3. **Generate Rust Bindings**

   The script invokes `bindgen::builder()` with configuration:
   - **Header file**: The appropriate wrapper header
   - **Include paths**: From the bindgen specs
   - **Derive traits**: `Default` and `Debug` for generated structs
   - **Dynamic library**: Named `libmxl`, requiring all symbols to be present
   - **Name transformations**: The custom `CB` callback

   The callback is crucial. It transforms C names to Rust conventions:
   - **Functions**: `mxlFooBar` becomes `foo_bar` (snake_case, prefix removed)
   - **Types**: `mxlFooBar` becomes `FooBar` (prefix removed, CamelCase preserved)

   The `to_snake_case` helper converts function names from CamelCase to snake_case by inserting underscores before uppercase letters and lowercasing them.

   The generated bindings are written to `$OUT_DIR/bindings.rs`, which is then included by `lib.rs`.

## The Flow of Data

At build time, the flow looks like this:

```
Cargo invokes build.rs
    |
    +--> CMake builds libmxl.so from C source (unless mxl-not-built)
    |
    +--> bindgen parses C headers with Clang
    |       |
    |       +--> Custom callback transforms names (mxlCreateInstance -> create_instance)
    |       |
    |       +--> Generates Rust struct and function declarations
    |
    +--> Writes bindings.rs to OUT_DIR
    |
    +--> lib.rs includes bindings.rs
```

At runtime, the flow looks like this:

```
mxl crate calls load_api("libmxl.so")
    |
    +--> libloading opens the shared library
    |
    +--> bindgen's dynamic library support maps function names
    |
    +--> Returns an MxlApi handle with callable function pointers
    |
    +--> Safe wrapper uses the handle to call C functions
```

## The Architecture

This is a **sys crate** in Rust terminology: a thin, unsafe wrapper around a C library. It follows the Rust convention:
- `-sys` crates expose raw FFI bindings
- Parent crates (like `mxl`) provide safe abstractions

The sys crate handles:
- Build-time compilation of C code via CMake
- Runtime dynamic loading via `libloading`
- Automatic code generation via `bindgen`
- Name transformations for Rust idioms

The safe wrapper handles:
- Memory safety via RAII types
- Thread safety via Send/Sync markers
- Error handling via Result types
- Lifetime management via Drop impls

This separation of concerns is the Rust way: keep the unsafe, foreign code isolated in a sys crate, and build safe abstractions on top.
