// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Logging.hpp
 * @brief Exception-safe logging macros for MXL internal diagnostics
 *
 * This file provides a thin wrapper around spdlog for all MXL internal logging.
 * Key design decisions:
 * - All macros wrap log calls in try/catch to prevent logging from crashing the application
 * - In debug builds (NDEBUG not defined), TRACE and DEBUG logs are compiled in
 * - In release builds, TRACE and DEBUG calls compile to nothing (zero overhead)
 * - Runtime log level is still controlled by MXL_LOG_LEVEL environment variable
 * - This approach balances diagnostics capability with production performance
 */

#pragma once

// In debug mode we keep all log statements.
// In release mode we only consider info and up.
// See : https://github.com/gabime/spdlog/wiki/0.-FAQ#how-to-remove-all-debug-statements-at-compile-time-
//
// Actual logging levels can be configured through the MXL_LOG_LEVEL environment variable
//
//
// Set the compile-time active log level based on build type
// This controls which log statements are actually compiled into the binary
#ifndef SPDLOG_ACTIVE_LEVEL
#   ifndef NDEBUG
       // Debug builds: include all log levels down to TRACE for maximum diagnostics
#      define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#   else
       // Release builds: only include INFO and above (TRACE/DEBUG compile to nothing)
#      define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#   endif
#endif

#include <spdlog/spdlog.h>

/**
 * MXL_TRACE: Most verbose logging for detailed execution traces
 * Used for fine-grained debugging of flow operations, memory accesses, ring buffer states.
 * Only compiled in debug builds. Wrapped in try/catch to ensure logging never crashes.
 */
#define MXL_TRACE(...)                 \
    do                                 \
    {                                  \
        try                            \
        {                              \
            SPDLOG_TRACE(__VA_ARGS__); \
        }                              \
        catch (...)                    \
        {}                             \
    }                                  \
    while (false)
/**
 * MXL_DEBUG: Debug-level logging for development diagnostics
 * Used for non-critical operational information useful during development.
 * Only compiled in debug builds. Exception-safe.
 */
#define MXL_DEBUG(...)                 \
    do                                 \
    {                                  \
        try                            \
        {                              \
            SPDLOG_DEBUG(__VA_ARGS__); \
        }                              \
        catch (...)                    \
        {}                             \
    }                                  \
    while (false)

/**
 * MXL_INFO: Informational logging for normal operations
 * Used for flow creation, reader/writer connections, configuration changes.
 * Compiled in all builds. Exception-safe.
 */
#define MXL_INFO(...)                 \
    do                                \
    {                                 \
        try                           \
        {                             \
            SPDLOG_INFO(__VA_ARGS__); \
        }                             \
        catch (...)                   \
        {}                            \
    }                                 \
    while (false)

/**
 * MXL_WARN: Warning-level logging for potential issues
 * Used for recoverable errors, deprecated APIs, performance concerns, stale flows.
 * Compiled in all builds. Exception-safe.
 */
#define MXL_WARN(...)                 \
    do                                \
    {                                 \
        try                           \
        {                             \
            SPDLOG_WARN(__VA_ARGS__); \
        }                             \
        catch (...)                   \
        {}                            \
    }                                 \
    while (false)

/**
 * MXL_ERROR: Error-level logging for operation failures
 * Used for failed system calls, invalid states, synchronization errors.
 * Compiled in all builds. Exception-safe.
 */
#define MXL_ERROR(...)                 \
    do                                 \
    {                                  \
        try                            \
        {                              \
            SPDLOG_ERROR(__VA_ARGS__); \
        }                              \
        catch (...)                    \
        {}                             \
    }                                  \
    while (false)

/**
 * MXL_CRITICAL: Critical-level logging for unrecoverable failures
 * Used for catastrophic errors that prevent MXL from functioning correctly.
 * Compiled in all builds. Exception-safe.
 */
#define MXL_CRITICAL(...)                 \
    do                                    \
    {                                     \
        try                               \
        {                                 \
            SPDLOG_CRITICAL(__VA_ARGS__); \
        }                                 \
        catch (...)                       \
        {}                                \
    }                                     \
    while (false)
