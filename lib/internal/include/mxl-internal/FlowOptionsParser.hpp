// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowOptionsParser.hpp
 * @brief Parse MXL-specific flow creation options (separate from NMOS definition)
 *
 * While NMOS describes the media format, MXL needs additional operational hints:
 * - How much data does the writer commit at once? (maxCommitBatchSizeHint)
 * - How often should readers be woken? (maxSyncBatchSizeHint)
 *
 * These options optimize:
 * - Ring buffer sizing (prevent overruns)
 * - Synchronization overhead (batch wakeups vs. per-sample)
 * - CPU utilization (reduce futex syscalls)
 *
 * Options can come from:
 * 1. Explicit JSON passed to createFlowWriter(..., options)
 * 2. Domain-wide defaults in ${domain}/options.json
 * 3. MXL internal defaults if not specified
 *
 * Example options JSON:
 * {
 *   "maxCommitBatchSizeHint": 1920,    // Writer commits at most 1920 samples/slices at once
 *   "maxSyncBatchSizeHint": 1920       // Wake readers every 1920 samples/slices
 * }
 *
 * Design:
 * - std::optional fields (no value = use MXL defaults)
 * - Immutable after construction (thread-safe for reads)
 * - Validated during parsing (throws on invalid values)
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <picojson/picojson.h>
#include <mxl/platform.h>

namespace mxl::lib
{
    /**
     * Parses flow options JSON and extracts valid attributes.
     * Options are hints for ring buffer sizing and synchronization behavior.
     *
     * Thread-safety: Immutable after construction (thread-safe for reads).
     */
    class MXL_EXPORT FlowOptionsParser
    {
    public:
        /** Default constructor: no options (use MXL defaults). */
        FlowOptionsParser() = default;

        /**
         * Parse a JSON string of flow options.
         *
         * Expected fields (all optional):
         * - "maxCommitBatchSizeHint": uint32 (samples or slices per commit)
         * - "maxSyncBatchSizeHint": uint32 (samples or slices per wakeup)
         *
         * @param in_flowOptions JSON string with options
         * @throws std::runtime_error if JSON is malformed or values are invalid
         */
        FlowOptionsParser(std::string const& in_flowOptions);

        /**
         * Get the maximum commit batch size hint.
         *
         * For continuous flows (AUDIO):
         * - Max samples committed per write operation
         * - Must be < (buffer_length / 2) to prevent overruns
         * - Example: 1920 samples (one video frame worth of audio at 48kHz/24fps)
         *
         * For discrete flows (VIDEO/DATA):
         * - Max slices (scan lines) committed before sync
         * - Must be >= 1
         * - Example: 1080 (commit full frame at once)
         *
         * Why this matters:
         * - Ring buffer sizing (must hold at least 2x commit batch size)
         * - Writer latency (larger batches = fewer commits but higher latency)
         * - Reader expectations (know how much new data might appear at once)
         *
         * @return Hint value if specified, std::nullopt if using defaults
         */
        [[nodiscard]]
        std::optional<std::uint32_t> getMaxCommitBatchSizeHint() const;

        /**
         * Get the maximum synchronization batch size hint.
         *
         * Controls how often the writer wakes readers (futex overhead vs. latency tradeoff).
         *
         * For continuous flows (AUDIO):
         * - Wake readers every N samples
         * - Must be multiple of maxCommitBatchSizeHint
         * - Example: 1920 (wake once per video frame's worth of audio)
         *
         * For discrete flows (VIDEO/DATA):
         * - Wake readers every N slices
         * - Example: 1 (wake after each commit), 1080 (wake after full frame)
         *
         * Why this matters:
         * - CPU usage: More frequent wakeups = more syscalls = higher CPU
         * - Latency: Less frequent wakeups = readers wait longer for new data
         * - Typical: Match video frame boundaries (wake per frame, not per slice)
         *
         * @return Hint value if specified, std::nullopt if using defaults
         */
        [[nodiscard]]
        std::optional<std::uint32_t> getMaxSyncBatchSizeHint() const;

        /**
         * Generic accessor for arbitrary JSON fields.
         *
         * Allows extracting custom options not explicitly handled by this parser.
         * Type T must match the JSON value type (string, int64_t, double, bool).
         *
         * @tparam T Expected C++ type (automatically deduced from picojson)
         * @param field JSON field name
         * @return Field value cast to T
         * @throws std::invalid_argument if field not found or type mismatch
         *
         * Example: auto custom = parser.get<int64_t>("customHint");
         */
        template<typename T>
        [[nodiscard]]
        T get(std::string const& field) const;

    private:
        /**
         * Max synchronization batch size (samples or slices per futex wakeup).
         * std::nullopt = use MXL defaults.
         * See getMaxSyncBatchSizeHint() for semantics.
         */
        std::optional<std::uint32_t> _maxSyncBatchSizeHint;

        /**
         * Max commit batch size (samples or slices per write operation).
         * std::nullopt = use MXL defaults.
         * See getMaxCommitBatchSizeHint() for semantics.
         */
        std::optional<std::uint32_t> _maxCommitBatchSizeHint;

        /**
         * Parsed JSON object (picojson).
         * Retained for generic field access via get<T>() template.
         * Contains full options JSON.
         */
        picojson::object _root;
    };

}
