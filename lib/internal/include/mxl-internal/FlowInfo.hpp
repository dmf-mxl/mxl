// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowInfo.hpp
 * @brief Stream output operator for mxlFlowInfo debugging
 *
 * Provides a human-readable representation of mxlFlowInfo structures for logging
 * and diagnostics. This is an internal utility used by MXL's logging infrastructure.
 *
 * The mxlFlowInfo structure (defined in the public API header mxl/flow.h) contains:
 * - Common metadata: format, grain rate, payload size, etc.
 * - Flow-type-specific info: videoFlowInfo or audioFlowInfo union
 *
 * This operator enables:
 *   MXL_INFO("Flow created: {}", flowInfo);  // Logs readable flow description
 */

#pragma once

#include <iosfwd>
#include <mxl/flow.h>
#include <mxl/platform.h>

/**
 * Stream insertion operator for mxlFlowInfo.
 *
 * Formats the flow info structure as human-readable text, including:
 * - Flow format (VIDEO_V210, AUDIO_F32, DATA_ST291, etc.)
 * - Grain rate (frame rate or sample rate)
 * - For video: resolution, interlacing, color model
 * - For audio: channel count, sample format
 * - Payload sizes and ring buffer parameters
 *
 * @param os Output stream to write to
 * @param obj Flow info structure to format
 * @return Reference to os (for chaining)
 *
 * Example output:
 *   "VIDEO_V210 1920x1080p59.94 (24000/1001) 16 grains"
 */
MXL_EXPORT
std::ostream& operator<<(std::ostream& os, mxlFlowInfo const& obj);
