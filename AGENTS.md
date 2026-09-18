<!-- SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Instructions for coding agents

These instructions apply throughout this repository. Read [CONTRIBUTING.md](CONTRIBUTING.md)
and preserve existing user changes. Keep edits focused on the requested work.  The policies
and guidelines in this document apply to all agent-generated contributions such as code,
documentation, build files, etc.

## SPDX notices

Follow the [copyright notice requirements](CONTRIBUTING.md#copyright-notices).
Begin new source files with the project's SPDX copyright and license notice,
using the year of creation:

```cpp
// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
```

Use comment syntax appropriate to the file type, such as `#` for scripts and
`<!-- ... -->` for Markdown. Preserve existing copyright holders, years, and
license identifiers when editing files. Follow the applicable license for
documentation and third-party files; do not replace their notices with the
source-code license. For formats that cannot contain comments, follow the
repository's adjacent `.license` file convention.

## Commit sign-off

Follow [Commit Sign-Off](CONTRIBUTING.md#commit-sign-off): every commit must
contain a `Signed-off-by: Name <email>` trailer. When creating a commit, use:

```sh
git commit --signoff
```

Use the contributor's configured Git identity. The contribution guidelines
require an email address from the organization that owns the contribution's
copyright. Do not invent an identity or sign off as another contributor; ask
for missing identity information if needed. Preserve existing sign-offs when
amending or carrying commits forward, and verify the resulting commit message.
A DCO sign-off is a commit-message trailer, separate from cryptographic commit
signing with `git commit -S`.

The contributor must review and understand AI-assisted changes as required by
`CONTRIBUTING.md`. These instructions do not themselves request a commit.

Commiting code must be initiated by the contributor, not a coding agent. Signing off a commit implies that the contributor has reviewed and understands every line of the commit, including AI-assisted ones.

## Programming Languages

- C++ code shall conform to the ISO C++20 standard.
- Compiler-specific extensions and non-standard language features shall not be used.
- C++20 modules are explicitly excluded from the supported language features.
- C code shall conform to the ISO C17 standard.

## Variable initialization

For new or modified C++ local variables, use `auto name = Type{...}`. Use
`const auto` for values that do not change and `constexpr auto` for compile-time
constants. Let clang-format place qualifiers according to `.clang-format`;
the resulting `auto const` spelling follows the same rule.

```cpp
auto index = std::uint64_t{0};
auto info = mxlEventInfo{};
auto payload = std::vector<std::uint8_t>{};
auto const capacity = std::size_t{4096};
constexpr auto alignment = std::size_t{64};
```

Use right-hand qualifier:

```cpp
auto const w = ...;
int* const x = ...;
float const* y = ...;
char const* const z = ....;
```

Use strongly typed literals (if available for the needed type) to deduce the correct variable type:

```cpp
auto w = 7U;    // unsigned int
auto x = 13L;   // long
auto y = 17ULL; // unsigned long long
auto z = 2Z;    // std::size_t if target standard is C++23
auto h = 1.5f;  // float
auto i = 2.0;   // double
auto c = '5';   // char
```

- Value-initialize variables; do not introduce uninitialized local storage.
- Avoid forms such as `Type name;`, `Type name{}`, and `Type name(args)` for
  local variables when the `auto name = Type{...}` form applies.
- For function results, casts, references, iterators, and structured bindings,
  use the appropriate `auto` declaration without a redundant conversion or copy:
  `auto const result = ring.read(index, info, payload);` or `auto& slot = slots[index];`.
- Preserve constructor semantics. For example, `std::vector<std::uint8_t>{count}`
  can select an initializer-list constructor instead of allocating `count`
  elements. Initialize an empty vector and call `resize(count)` when appropriate or
  explicitly use parentheses in this case as an exception.
- C declarations, function signatures, and non-static data members require
  explicit types. Use brace initialization for C++ members where applicable;
  keep public C headers valid C.

An exception to these rules is simple initialisation of pointers with a default value of nulllptr (since the auto type inference will not work unless to statically cast nullptr to the desired type). The following syntax is clearer and more concise:

```cpp
Type *ptr = nullptr;
```

- Contructors and initialization: default constructor initializer lists should be used over default member initializers, except in cases of small trivial, standard layout structs used in ad-hoc contexts.

## Formatting with clang-format

Use the repository's [.clang-format](.clang-format) without overriding its style
options. Use a clang-format version that supports the configuration, preferably
the version used by CI. Do not remove configuration options to accommodate an
older formatter.

Format the C and C++ files you changed, then check them:

```sh
clang-format -i path/to/changed.cpp path/to/changed.hpp
clang-format --dry-run --Werror path/to/changed.cpp path/to/changed.hpp
git diff --check
```

Review the resulting diff, including qualifier placement and comments. Avoid
formatting unrelated files or generated and third-party code.

## Static analysis with clang-tidy

Use [.clang-tidy](.clang-tidy) and the compilation database from the configured
build directory. CMake presets enable `CMAKE_EXPORT_COMPILE_COMMANDS`; keep the
database current after changing dependencies or build settings.

In the following commands, replace `<build-dir>`, `<preset>`, and source paths
with the values for the current platform, such as `build/Darwin-Clang-Debug`
and `Darwin-Clang-Debug`.

```sh
clang-tidy -p <build-dir> --config-file=.clang-tidy path/to/changed.cpp
```

For a header change, analyze a source file that includes it using its real build
flags. A focused header filter can make the relevant diagnostics easier to review:

```sh
clang-tidy -p <build-dir> --config-file=.clang-tidy --header-filter='EventRingBuffer\.hpp$' lib/internal/src/PosixEventFlowWriter.cpp
```

For analysis during compilation, the project also supports:

```sh
cmake --preset <preset> -DMXL_ENABLE_CLANG_TIDY=ON
cmake --build <build-dir>
```

Fix warnings introduced by the change and rerun the affected checks. Prefer a
code correction over disabling a check or adding `NOLINT`. If a suppression is
necessary, name the specific check and document why it cannot be resolved in
code. Inspect diagnostics as well as the exit status: warnings need not make
clang-tidy fail. Report unrelated existing warnings separately.

## Doxygen documentation

Every new class, structure, enum, enum value, member, constant, and function
must have valid Doxygen documentation. This includes internal and private
declarations, constructors, destructors, test helpers, and tool code.

- Use `/** ... */` or `///` before declarations and `///<` for member or enum
  value documentation. Ordinary `//` comments do not replace API documentation.
- Describe the purpose and contract, rather than repeating the declaration.
- Document every parameter with its exact name and applicable direction using
  `@param`, `@param[in]`, `@param[out]`, or `@param[in,out]`.
- Document returned values with `@return` or individual status codes with
  `@retval`. Document applicable exceptions, preconditions, and postconditions.
- Explain units, defaults, valid ranges, ownership, pointer lifetime, thread
  safety, and shared-memory layout constraints where relevant.
- Use `@copydoc` only when the referenced declaration exists and its contract
  applies. Add `@file` documentation to new source and header files, and use
  `@test` for new test cases.
- Update documentation when behavior changes, including related Markdown
  guides and examples. Remove obsolete guarantees and descriptions.

Build the API documentation when adding or changing documented declarations:

```sh
cmake --preset <preset> -DBUILD_DOCS=ON
cmake --build <build-dir> --target doc
```

Inspect warnings and generated documentation for the changed declarations.
The normal `Doxyfile.in` uses `EXTRACT_ALL=YES`, so a successful build alone
does not prove documentation coverage. Audit new declarations explicitly; for
a focused warning check, use a temporary configuration with `EXTRACT_ALL=NO`,
undocumented and parameter warnings enabled, and private/static declarations
included. Include changed tool or test files if the normal input excludes them.
Do not weaken the repository's documentation checks to hide missing comments.

## Verification and reporting

After code changes, rebuild the affected targets and run relevant existing
tests. Add regression coverage for behavior changes when needed. Run the full
suite for broad changes such as dependency updates. Report which checks ran,
their results, and any failures or unavailable tools; do not claim checks passed
without running them. Documentation-only edits need a content and diff review,
not an unrelated rebuild.
