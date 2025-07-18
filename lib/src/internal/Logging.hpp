// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

// In debug mode we keep all log statements.
// In release mode we only consider info and up.
// See : https://github.com/gabime/spdlog/wiki/0.-FAQ#how-to-remove-all-debug-statements-at-compile-time-
//
// Actual logging levels can be configured through the MXL_LOG_LEVEL environment variable
//
//
#ifndef SPDLOG_ACTIVE_LEVEL
#   ifndef NDEBUG
#      define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#   else
#      define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#   endif
#endif

#include <spdlog/spdlog.h>

#define MXL_TRACE    SPDLOG_TRACE
#define MXL_DEBUG    SPDLOG_DEBUG
#define MXL_INFO     SPDLOG_INFO
#define MXL_WARN     SPDLOG_WARN
#define MXL_ERROR    SPDLOG_ERROR
#define MXL_CRITICAL SPDLOG_CRITICAL
