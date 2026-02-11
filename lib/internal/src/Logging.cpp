// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Logging.cpp
 * @brief Implements logging infrastructure for the MXL library
 *
 * This file serves as the translation unit for the logging system, which is primarily
 * header-based. The actual logging macros (MXL_ERROR, MXL_WARN, MXL_INFO, MXL_DEBUG, MXL_TRACE)
 * are defined in the header file.
 *
 * The logging system is based on spdlog and provides:
 * - Hierarchical log levels (error, warn, info, debug, trace)
 * - Environment variable configuration via MXL_LOG_LEVEL
 * - Color-coded console output
 * - Format string support via fmt library
 *
 * Log initialization happens lazily in Instance.cpp using std::call_once to ensure
 * thread-safe initialization.
 *
 * @see Logging.hpp for macro definitions
 * @see Instance.cpp for initialization logic
 */

#include "mxl-internal/Logging.hpp"
