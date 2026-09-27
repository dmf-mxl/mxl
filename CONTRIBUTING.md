<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Contributing

Thank you for your interest in contributing to MXL. This document explains our contribution process and procedures.
For a description of the roles and responsibilities of the various members of the MXL community, see the GOVERNANCE file, and for further details, see the project's Technical
Charter. Briefly, Contributors submit content to the project, Maintainers review and approve these submissions, and the Technical Steering Committee (TSC) oversees the project as a whole.

## How to engage with the project

There are several ways to connect with the MXL project:

- Join the email distribution list. To do so, please click the "Join our group" button at the [EBU MXL group page](https://tech.ebu.ch/dmf/mxl). You will receive the distribution list address, as well as links to register for the TSC meetings, the RC meetings, and to join the MXL Slack workspace, in the confirmation email after joining the group. The regular TSC and RC meetings are free to attend for all interested parties. Please note that you will need to create an account on the EBU page to proceed. When joining the TSC or RC meetings, please indicate your full name and affiliation.
- Slack workspace. To join the MXL Slack workspace, please follow the procedure above for joining the email distribution list. We use a Slack workspace for punctual questions and direct messages only, as the conversation history will not be retained. In contrast, important questions should be documented in GitHub discussions (see the section below). When joining the MXL Slack workspace, ensure your display name includes your full name and affiliation. Failure to comply with this rule will result in the removal of your account from the workspace.

## How to Ask for Help

If you have questions about implementing, using, or extending MXL, please use the MXL [GitHub discussions](https://github.com/dmf-mxl/mxl/discussions).

## How to Report a Bug or Request a Feature

MXL utilises GitHub's issue tracking system for bug reports and feature requests: <https://github.com/dmf-mxl/mxl/issues>.
If submitting a bug report, include:

- Which plug-in(s) and host products are involved
- OS and compilers used
- Any special build flags or environmental issues
- A detailed, reproducible description:
  - What you tried
  - What happened
  - What you expected to happen
- If possible, a minimalistic sample reproducing the issue

## How to Report a Security Vulnerability

If you think you've found a potential vulnerability in MXL, please refer to [SECURITY.md](SECURITY.md) to responsibly disclose it.

## How to Contribute a Bug Fix or Change

Before contributing code, please review the [GOVERNANCE](GOVERNANCE/GOVERNANCE.md) file to understand the roles involved.
You'll need:

- A good knowledge of git.
- A fork of the GitHub repo.
- An understanding of the project's development workflow.

A contribution to the project must be reviewed and understood in its entirety by the contributor. AI assisted contributions are allowed but the rule remains the same: the contributor must understand every line of the contribution and should be able to justify all of them.

## Legal Requirements

MXL follows the Linux Foundation’s best practices for open-source contributions.

## License

MXL is licensed under the [Apache-2.0](LICENSE.txt) license. Contributions must comply with this license.

## Contributor License Agreements

We do not require a CLA at this time.

## Commit Sign-Off

Every commit must be signed off.  That is, every commit log message must include a “`Signed-off-by`” line (generated, for example, with “`git commit --signoff`”), indicating that the Contributor wrote the code and has the right to release it under the [Apache-2.0](LICENSE.txt) license.

Contributors are responsible for providing their email address from the organization's domain that owns the copyright for their committed contribution, ensuring the copyright holder can be tracked.

# Development Workflow

## Git Basics

Working with MXL requires understanding a significant amount of Git and GitHub-based terminology. If you’re unfamiliar with these tools or their lingo, please look at the [GitHub Glossary](https://docs.github.com/en/get-started/learning-about-github/github-glossary) or browse [GitHub Help](https://help.github.com/).
To contribute, you need a GitHub account. This is needed in order to push changes to the upstream repository via a pull request.
You will also need Git installed on your local development machine. If you need setup assistance, please see the official [Git Documentation](https://git-scm.com/doc).

## Repository Structure and Commit Policy

The main branch represents the stable release-ready branch and is protected. Development is typically done in feature branches that are merged into main via pull requests.

## Fork & Clone

1. Fork the repository on GitHub.
2. Clone your fork locally.
3. Add the upstream MXL repository as a remote.

## Pull Requests

Contributions are submitted via pull requests. Follow this workflow:

1. Create a topic branch named feature/\<your-feature\> or bugfix/\<your-fix\> from the latest main.
2. Make changes, test thoroughly, and match existing code style.
3. Push commits to your fork.
4. Open a pull request against the main branch.
5. PRs are reviewed by Maintainers and Contributors.
Note: The main branch is protected. All changes must go through reviewed pull requests.

## Code Review and Required Approvals

To comply with the MXL project’s rules:

- All PRs to the main branch must receive at least 2 Maintainer approvals.
- The PR must be up-to-date with the **branch** before it can be merged.
- The following status checks must pass:
  - DCO
  - Build on Ubuntu 24.04 - arm64 - Linux-Clang-Release
  - Build on Ubuntu 24.04 - arm64 - Linux-GCC-Release
  - Build on Ubuntu 24.04 - x86_64 - Linux-Clang-Release
  - Build on Ubuntu 24.04 - x86_64 - Linux-GCC-Release
Only Maintainers who are not authors of the PR may approve the merge.
Disagreements or blocked changes may be escalated to the TSC for resolution. Maintainers may assign the tsc-review label to request such escalation.

## Test Policy

This project uses [Catch2](https://github.com/catchorg/Catch2) automated testing framework. This framework is a modern, lightweight, and compatible with CMake/CTest and nicely integrated with VSCode [C++ Test Mate](https://marketplace.visualstudio.com/items?itemName=matepek.vscode-catch2-test-adapter) extension.

## Copyright Notices

According to the [project Charter](https://github.com/dmf-mxl/mxl/blob/main/GOVERNANCE/CHARTER.pdf) (section #8.a.), contribution copyright remains with the copyright holders. This can be tracked using the project's git commit history.

All new source files should begin with a copyright and license stating:

```
// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
```

## Third-party libraries

The use of third-party libraries is allowed. If these libraries don't match the Apache 2 license, they need to be approved by the TSC. Dependencies of the library itself should be on small-sized libraries, not frameworks, as the goal is to keep the code lightweight.

## Comments and Doxygen

Documentation is automatically generated by Doxygen. A documentation target is available in the cmake generate build files.

`ninja doc`

## Logging

This library uses [spdlog](https://github.com/gabime/spdlog) for internal logging. Logging is disabled by default but can be enabled by setting the MXL_LOG_LEVEL environment variable to one of : off, critical, error, warn, info, debug, trace.
At the moment the logs are going to stdout.

_Note: the debug and trace log statements are statically excluded from the library at compilation time in release mode._

## Coding standards

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
- Add parentheses around the individual binary boolean expressions.

An exception to these rules is simple initialisation of pointers with a default value of nulllptr (since the auto type inference will not work unless to statically cast nullptr to the desired type). The following syntax is clearer and more concise:

```cpp
Type *ptr = nullptr;
```

- Contructors and initialization: default constructor initializer lists should be used over default member initializers, except in cases of small trivial, standard layout structs used in ad-hoc contexts.

## Formatting with clang-format

Code formatting rules are defined in the [.clang-format](.clang-format) configuration file
found at the root of the repository.

## Static analysis with clang-tidy

Source code static analysis is performed using cland-tidy.  The [.clang-tidy](.clang-tidy)
configuration file is found in the root directory of the repository.

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
- Update documentation when behavior changes, including related Markdown
  guides and examples. Remove obsolete guarantees and descriptions.

Build the API documentation when adding or changing documented declarations:

```sh
# in your build directory:
ninja doc
```

Inspect warnings and generated documentation for the changed declarations.
