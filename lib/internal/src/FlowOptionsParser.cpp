// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowOptionsParser.hpp"
#include <picojson/picojson.h>
#include <mxl/mxl.h>
#include "mxl-internal/JsonUtils.hpp"
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    FlowOptionsParser::FlowOptionsParser(std::string const& in_options)
    {
        if (in_options.empty())
        {
            return;
        }

        //
        // Parse the json options
        //
        auto jsonValue = picojson::value{};
        auto const err = parseJson(jsonValue, in_options);
        if (!err.empty())
        {
            throw std::invalid_argument{"Invalid JSON options. " + err};
        }

        // Confirm that the root is a json object
        if (!jsonValue.is<picojson::object>())
        {
            throw std::invalid_argument{"Expected a JSON object"};
        }
        _root = jsonValue.get<picojson::object>();

        auto maxCommitBatchSizeHintIt = _root.find("maxCommitBatchSizeHint");
        if (maxCommitBatchSizeHintIt != _root.end())
        {
            if (!maxCommitBatchSizeHintIt->second.is<double>())
            {
                throw std::invalid_argument{"maxCommitBatchSizeHint must be a number."};
            }

            auto const v = jsonToInteger<std::uint32_t>(maxCommitBatchSizeHintIt->second, "maxCommitBatchSizeHint");
            if (v < 1U)
            {
                throw std::invalid_argument{"maxCommitBatchSizeHint must be greater or equal to 1."};
            }
            _maxCommitBatchSizeHint = v;
        }

        auto maxSyncBatchSizeHintIt = _root.find("maxSyncBatchSizeHint");
        if (maxSyncBatchSizeHintIt != _root.end())
        {
            if (!maxSyncBatchSizeHintIt->second.is<double>())
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be a number."};
            }

            auto const v = jsonToInteger<std::uint32_t>(maxSyncBatchSizeHintIt->second, "maxSyncBatchSizeHint");
            if (v < 1U)
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be greater or equal to 1."};
            }
            _maxSyncBatchSizeHint = v;
            if ((_maxSyncBatchSizeHint.value() % _maxCommitBatchSizeHint.value_or(1) != 0))
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be a multiple of maxCommitBatchSizeHint."};
            }
        }
    }

    std::optional<std::uint32_t> FlowOptionsParser::getMaxCommitBatchSizeHint() const
    {
        return _maxCommitBatchSizeHint;
    }

    std::optional<std::uint32_t> FlowOptionsParser::getMaxSyncBatchSizeHint() const
    {
        return _maxSyncBatchSizeHint;
    }
} // namespace mxl::lib
