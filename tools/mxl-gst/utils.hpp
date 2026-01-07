// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <fmt/format.h>
#include <picojson/picojson.h>
#include "mxl/rational.h"

/**
 * Logging macros
 */

// clang-format off
#define MXL_LOG(level, msg, ...)                          \
    do                                                    \
    {                                                     \
        std::cerr                                         \
            << '[' << level << "] " << " - "              \
            << fmt::format(msg __VA_OPT__(,) __VA_ARGS__) \
            << std::endl;                                 \
    }                                                     \
    while (0)

#define MXL_ERROR(msg, ...) MXL_LOG("ERROR", msg __VA_OPT__(,) __VA_ARGS__)
#define MXL_WARN(msg, ...)  MXL_LOG("WARN",  msg __VA_OPT__(,) __VA_ARGS__)
#define MXL_INFO(msg, ...)  MXL_LOG("INFO",  msg __VA_OPT__(,) __VA_ARGS__)
#define MXL_DEBUG(msg, ...) MXL_LOG("DEBUG", msg __VA_OPT__(,) __VA_ARGS__)
#define MXL_TRACE(msg, ...) MXL_LOG("TRACE", msg __VA_OPT__(,) __VA_ARGS__)
// clang-format on

namespace media_utils
{
    std::uint32_t getV210LineLength(std::size_t width)
    {
        return static_cast<std::uint32_t>((width + 47) / 48 * 128);
    }
}

namespace json_utils
{
    /**
     * Parse a JSON buffer and return the root object.  Throws on error.
     * @param jsonBuffer The JSON buffer to parse.
     * @return The root JSON object.
     */
    picojson::object parseBuffer(std::string const& jsonBuffer)
    {
        // Parse the JSON
        auto root = picojson::value{};
        auto const err = picojson::parse(root, jsonBuffer);
        if (!err.empty())
        {
            throw std::runtime_error("JSON parse error in " + jsonBuffer + ": " + err);
        }

        // Ensure root is an object
        if (!root.is<picojson::object>())
        {
            throw std::runtime_error("Root JSON value is not an object in " + jsonBuffer);
        }

        // Return the root object
        return root.get<picojson::object>();
    }

    /**
     * Parse a JSON file and return the root object.  Throws on error.
     * @param path jsonFile path to the JSON file.
     * @return The root JSON object.
     */
    picojson::object parseFile(std::filesystem::path const& jsonFile)
    {
        // Open the file
        auto ifs = std::ifstream{jsonFile};
        if (!ifs.is_open())
        {
            throw std::runtime_error("Failed to open JSON file: " + jsonFile.string());
        }

        // Read it all into a string buffer
        auto buffer = std::stringstream{};
        buffer << ifs.rdbuf();

        return parseBuffer(buffer.str());
    }

    /**
     * Accessor for a json field with error checking.  Throws if the field is missing or of the wrong type.
     * @param obj The JSON object.
     * @param name The field name.
     * @return The field value.
     */
    template<typename T>
    T getField(picojson::object const& obj, std::string const& name)
    {
        auto it = obj.find(name);
        if (it == obj.end())
        {
            throw std::runtime_error("Missing JSON field: " + name);
        }

        if (!it->second.is<T>())
        {
            throw std::runtime_error("JSON field '" + name + "' has unexpected type");
        }

        return it->second.get<T>();
    }

    /**
     * Accessor for a json field with a default value.  Returns the default if the field is missing or of the wrong type.
     * @param obj The JSON object.
     * @param name The field name.
     * @param default_value The default value to return if the field is missing or of the wrong type.
     * @return The field value or the default value.
     */
    template<typename T>
    T getFieldOr(picojson::object const& obj, std::string const& name, T const& default_value)
    {
        auto it = obj.find(name);
        if (it == obj.end() || !it->second.is<T>())
        {
            return default_value;
        }

        return it->second.get<T>();
    }

    /**
     * Accessor for a rational json field.  Throws if the field is missing or of the wrong type.
     * @param obj The JSON object.
     * @param name The field name.
     * @return The rational value.
     */
    mxlRational getRational(picojson::object const& obj, std::string const& name)
    {
        auto const rationalObj = getField<picojson::object>(obj, name);
        return mxlRational{
            static_cast<std::uint32_t>(getField<double>(rationalObj, "numerator")),
            static_cast<std::uint32_t>(getFieldOr<double>(rationalObj, "denominator", 1.0)),
        };
    }

    /**
     * Update the group-hint tag in the provided NMOS flow
     *
     * @param nmosFlow The json nmos flow (edited in place)
     * @param groupHint The group-hint value to set
     * @param roleInGroup The role-in-group value to set in the group-hint tag.
     */
    void updateGroupHint(picojson::object& nmosFlow, std::string const& groupHint, std::string const& roleInGroup)
    {
        auto jsonObj = nmosFlow; // make a copy to modify
        auto& tagsObj = jsonObj.find("tags")->second.get<picojson::object>();
        auto& tagsArray = tagsObj["urn:x-nmos:tag:grouphint/v1.0"].get<picojson::array>();
        tagsArray.clear();
        tagsArray.emplace_back(groupHint + ":" + roleInGroup);
    }

    /**
     * Serialize a picojson object to a string
     *
     * @param obj The picojson object to serialize
     * @return The serialized JSON string
     */
    std::string serializeJson(picojson::object const& obj)
    {
        return picojson::value{obj}.serialize();
    }

} // namespace json_utils
