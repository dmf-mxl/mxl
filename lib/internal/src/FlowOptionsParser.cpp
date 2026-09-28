// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowOptionsParser.hpp"
#include <picojson/picojson.h>
#include <mxl/mxl.h>
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PayloadBackend.hpp"

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
    }

    std::optional<std::uint32_t> FlowOptionsParser::getMaxCommitBatchSizeHint() const
    {
        return _maxCommitBatchSizeHint;
    }

    std::optional<std::uint32_t> FlowOptionsParser::getMaxSyncBatchSizeHint() const
    {
        return _maxSyncBatchSizeHint;
    }

    namespace
    {
        /** Name of the options object that describes the payload storage. */
        constexpr auto PAYLOAD_OPTION = "payload";

        /**
         * \param[in] what The problem with the "payload" options.
         * \return The error to throw.
         */
        PayloadStorageError invalidPayloadOption(std::string const& what)
        {
            return PayloadStorageError{MXL_ERR_INVALID_ARG, "Invalid \"payload\" option: " + what};
        }

        /**
         * \param[in] root The parsed options.
         * \return The "payload" object, or null if the options have none.
         * \throws PayloadStorageError if "payload" is not an object.
         */
        picojson::object const* findPayloadObject(picojson::object const& root)
        {
            auto const it = root.find(PAYLOAD_OPTION);
            if (it == root.end())
            {
                return nullptr;
            }
            if (!it->second.is<picojson::object>())
            {
                throw invalidPayloadOption("it must be an object.");
            }
            return &it->second.get<picojson::object>();
        }
    }

    PayloadStorageSpec FlowOptionsParser::getPayloadStorageSpec() const
    {
        auto spec = PayloadStorageSpec{};
        auto const* const payload = findPayloadObject(_root);
        if (payload == nullptr)
        {
            return spec;
        }

        auto backendOptions = picojson::object{};
        for (auto const& [key, value] : *payload)
        {
            if (key == "backend")
            {
                if (!value.is<std::string>() || !PayloadBackendLoader::isValidBackendName(value.get<std::string>()))
                {
                    throw invalidPayloadOption("\"backend\" must be a backend name of lowercase letters, digits and hyphens.");
                }
                spec.backend = value.get<std::string>();
            }
            else
            {
                backendOptions.emplace(key, value);
            }
        }

        if (spec.backend == BUILT_IN_HOST_PAYLOAD_BACKEND)
        {
            spec.backend.clear();
        }

        if (!backendOptions.empty())
        {
            if (!spec.usesBackend())
            {
                throw invalidPayloadOption("the built-in host storage accepts no options, but got \"" + backendOptions.begin()->first + "\".");
            }
            spec.backendOptions = picojson::value{backendOptions}.serialize();
        }
        return spec;
    }

    std::string FlowOptionsParser::getReaderPayloadOptions() const
    {
        auto const* const payload = findPayloadObject(_root);
        if ((payload == nullptr) || payload->empty())
        {
            return {};
        }
        if (payload->find("backend") != payload->end())
        {
            throw invalidPayloadOption("readers cannot select the storage with \"backend\".");
        }
        return picojson::value{*payload}.serialize();
    }
} // namespace mxl::lib
