// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowOptionsParser.hpp"
#include <stdexcept>
#include <string>
#include <picojson/picojson.h>
#include <mxl/mxl.h>
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    namespace
    {
        mxlPayloadLocation parsePayloadLocationValue(picojson::value const& value)
        {
            if (value.is<std::string>())
            {
                auto const& s = value.get<std::string>();
                if ((s == "host") || (s == "HOST") || (s == "host_memory"))
                {
                    return MXL_PAYLOAD_LOCATION_HOST_MEMORY;
                }
                if ((s == "device") || (s == "DEVICE") || (s == "device_memory"))
                {
                    return MXL_PAYLOAD_LOCATION_DEVICE_MEMORY;
                }
                throw std::invalid_argument{"payload location must be \"host\" or \"device\"."};
            }

            if (value.is<double>())
            {
                auto const v = static_cast<int>(value.get<double>());
                if (v == static_cast<int>(MXL_PAYLOAD_LOCATION_HOST_MEMORY))
                {
                    return MXL_PAYLOAD_LOCATION_HOST_MEMORY;
                }
                if (v == static_cast<int>(MXL_PAYLOAD_LOCATION_DEVICE_MEMORY))
                {
                    return MXL_PAYLOAD_LOCATION_DEVICE_MEMORY;
                }
                throw std::invalid_argument{"payload location numeric value must be 0 (host) or 1 (device)."};
            }

            throw std::invalid_argument{"payload location must be a string or number."};
        }

        int32_t parseDeviceIndexValue(picojson::value const& value)
        {
            if (!value.is<double>())
            {
                throw std::invalid_argument{"deviceIndex must be a number."};
            }
            return static_cast<int32_t>(value.get<double>());
        }
    }

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
        auto const err = picojson::parse(jsonValue, in_options);
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

            auto const v = maxCommitBatchSizeHintIt->second.get<double>();
            if (v < 1)
            {
                throw std::invalid_argument{"maxCommitBatchSizeHint must be greater or equal to 1."};
            }
            _maxCommitBatchSizeHint = static_cast<std::uint32_t>(v);
        }

        auto maxSyncBatchSizeHintIt = _root.find("maxSyncBatchSizeHint");
        if (maxSyncBatchSizeHintIt != _root.end())
        {
            if (!maxSyncBatchSizeHintIt->second.is<double>())
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be a number."};
            }

            auto const v = maxSyncBatchSizeHintIt->second.get<double>();
            if (v < 1)
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be greater or equal to 1."};
            }
            _maxSyncBatchSizeHint = static_cast<std::uint32_t>(v);
            if ((_maxSyncBatchSizeHint.value() % _maxCommitBatchSizeHint.value_or(1) != 0))
            {
                throw std::invalid_argument{"maxSyncBatchSizeHint must be a multiple of maxCommitBatchSizeHint."};
            }
        }

        // Nested payload object takes precedence when present.
        auto payloadIt = _root.find("payload");
        if (payloadIt != _root.end())
        {
            if (!payloadIt->second.is<picojson::object>())
            {
                throw std::invalid_argument{"payload must be a JSON object."};
            }
            auto const& payloadObj = payloadIt->second.get<picojson::object>();

            if (auto locIt = payloadObj.find("location"); locIt != payloadObj.end())
            {
                _payloadLocation = parsePayloadLocationValue(locIt->second);
            }

            if (auto deviceIt = payloadObj.find("deviceIndex"); deviceIt != payloadObj.end())
            {
                _deviceIndex = parseDeviceIndexValue(deviceIt->second);
            }
            else if (_payloadLocation == MXL_PAYLOAD_LOCATION_DEVICE_MEMORY)
            {
                _deviceIndex = 0;
            }

            if (auto backendIt = payloadObj.find("backend"); backendIt != payloadObj.end())
            {
                if (!backendIt->second.is<std::string>())
                {
                    throw std::invalid_argument{"payload.backend must be a string."};
                }
                _payloadBackend = backendIt->second.get<std::string>();
            }
        }
        else
        {
            if (auto locIt = _root.find("payloadLocation"); locIt != _root.end())
            {
                _payloadLocation = parsePayloadLocationValue(locIt->second);
            }

            if (auto deviceIt = _root.find("deviceIndex"); deviceIt != _root.end())
            {
                _deviceIndex = parseDeviceIndexValue(deviceIt->second);
            }
            else if (_payloadLocation == MXL_PAYLOAD_LOCATION_DEVICE_MEMORY)
            {
                _deviceIndex = 0;
            }

            if (auto backendIt = _root.find("payloadBackend"); backendIt != _root.end())
            {
                if (!backendIt->second.is<std::string>())
                {
                    throw std::invalid_argument{"payloadBackend must be a string."};
                }
                _payloadBackend = backendIt->second.get<std::string>();
            }
        }

        if (_payloadLocation == MXL_PAYLOAD_LOCATION_HOST_MEMORY)
        {
            if (_deviceIndex != -1)
            {
                throw std::invalid_argument{"deviceIndex must be -1 when payload location is host."};
            }
        }
        else if (_deviceIndex < 0)
        {
            throw std::invalid_argument{"deviceIndex must be >= 0 when payload location is device."};
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

    mxlPayloadLocation FlowOptionsParser::getPayloadLocation() const noexcept
    {
        return _payloadLocation;
    }

    int32_t FlowOptionsParser::getDeviceIndex() const noexcept
    {
        return _deviceIndex;
    }

    std::string const& FlowOptionsParser::getPayloadBackend() const noexcept
    {
        return _payloadBackend;
    }
} // namespace mxl::lib
