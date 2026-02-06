<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Building: Running Tests

## Run all tests

```bash
# Change to build directory
cd build/Linux-Clang-Debug

# Run all tests
ctest --output-on-failure
```

## Run specific tests

```bash
# List all available tests
ctest -N

# Run a specific test
ctest -R test_flows --output-on-failure

# Run tests matching a pattern
ctest -R "test_.*" --output-on-failure
```

## Run tests with verbose output

```bash
ctest --verbose
```

## Run tests in parallel

```bash
# Run tests using 4 parallel jobs
ctest -j4
```

[Back to Building overview](./Building.md)
