// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowOptionsParser.cpp
 * @brief Parses flow-specific configuration options from JSON
 *
 * FlowOptionsParser extracts optional performance tuning parameters from a JSON options
 * string passed during flow creation. These options allow applications to optimize MXL
 * behavior for their specific use cases.
 *
 * Supported options:
 * - maxCommitBatchSizeHint: How many slices/samples to write before checking readers
 * - maxSyncBatchSizeHint: How many slices/samples before waking blocked readers (futex)
 *
 * Batch sizing tradeoffs:
 * - Smaller batches: Lower latency, more futex wake calls, more CPU overhead
 * - Larger batches: Higher latency, fewer wake calls, better throughput
 *
 * The sync batch size must be a multiple of the commit batch size to maintain
 * consistent signaling behavior.
 *
 * Example JSON:
 * @code
 * {
 *   "maxCommitBatchSizeHint": 1080,
 *   "maxSyncBatchSizeHint": 1080
 * }
 * @endcode
 *
 * For video, a common pattern is to set both to the frame height (one line per commit,
 * one frame per sync), providing a good balance between latency and overhead.
 */

#include "mxl-internal/FlowOptionsParser.hpp"
#include <picojson/picojson.h>
#include <mxl/mxl.h>
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    /**
     * @brief Parse flow options from a JSON string
     *
     * This constructor parses the JSON options and extracts known fields. Empty
     * strings are treated as "no options" and result in all defaults being used.
     *
     * @param in_options JSON string containing flow options (may be empty)
     *
     * @throws std::invalid_argument if JSON is malformed
     * @throws std::invalid_argument if required type constraints are violated
     * @throws std::invalid_argument if sync batch is not a multiple of commit batch
     *
     * Validation rules:
     * - maxCommitBatchSizeHint: must be >= 1
     * - maxSyncBatchSizeHint: must be >= 1 and a multiple of commit batch
     */
    FlowOptionsParser::FlowOptionsParser(std::string const& in_options)
    {
        // Empty options string means use all defaults
        if (in_options.empty())
        {
            return;
        }

        //
        // Parse the JSON options string
        //
        auto jsonValue = picojson::value{};
        auto const err = picojson::parse(jsonValue, in_options);
        if (!err.empty())
        {
            throw std::invalid_argument{"Invalid JSON options. " + err};
        }

        // Confirm that the root is a JSON object (not array, string, etc.)
        if (!jsonValue.is<picojson::object>())
        {
            throw std::invalid_argument{"Expected a JSON object"};
        }
        _root = jsonValue.get<picojson::object>();

        //
        // Extract maxCommitBatchSizeHint (optional)
        //
        auto maxCommitBatchSizeHintIt = _root.find("maxCommitBatchSizeHint");
        if (maxCommitBatchSizeHintIt != _root.end())
        {
            // Validate type
            if (!maxCommitBatchSizeHintIt->second.is<double>())
            {
                throw std::invalid_argument{"maxCommitBatchSizeHint must be a number."};
            }

            // Validate range
            auto const v = maxCommitBatchSizeHintIt->second.get<double>();
            if (v < 1)
            {
                throw std::invalid_argument{"maxCommitBatchSizeHint must be greater or equal to 1."};
            }

            _maxCommitBatchSizeHint = static_cast<std::uint32_t>(v);
        }

        //
        // Extract maxSyncBatchSizeHint (optional)
        //
        auto maxSyncBatchSizeHintIt = _root.find("maxSyncBatchSizeHint");
        if (maxSyncBatchSizeHintIt != _root.end())
        {
            // Validate type
            if (!maxSyncBatchSizeHintIt->second.is<double>())
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be a number."};
            }

            // Validate range
            auto const v = maxSyncBatchSizeHintIt->second.get<double>();
            if (v < 1)
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be greater or equal to 1."};
            }

            _maxSyncBatchSizeHint = static_cast<std::uint32_t>(v);

            // Validate that sync batch is a multiple of commit batch
            // This ensures consistent signaling behavior
            if ((_maxSyncBatchSizeHint.value() % _maxCommitBatchSizeHint.value_or(1) != 0))
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be a multiple of maxCommitBatchSizeHint."};
            }
        }
    }

    /**
     * @brief Get the maximum commit batch size hint
     *
     * The commit batch size controls how frequently the writer checks for completion.
     * Smaller values reduce latency but increase overhead.
     *
     * @return Optional commit batch size (empty if not specified in options)
     */
    std::optional<std::uint32_t> FlowOptionsParser::getMaxCommitBatchSizeHint() const
    {
        return _maxCommitBatchSizeHint;
    }

    /**
     * @brief Get the maximum sync batch size hint
     *
     * The sync batch size controls how frequently blocked readers are awakened via futex.
     * This must be a multiple of the commit batch size.
     *
     * @return Optional sync batch size (empty if not specified in options)
     */
    std::optional<std::uint32_t> FlowOptionsParser::getMaxSyncBatchSizeHint() const
    {
        return _maxSyncBatchSizeHint;
    }
} // namespace mxl::lib
