// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file utils.hpp
 * @brief Shared utility functions for MXL GStreamer integration tools
 *
 * This header provides common functionality used by the mxl-gst tools:
 *   - Logging macros (MXL_ERROR, MXL_WARN, MXL_INFO, MXL_DEBUG, MXL_TRACE)
 *   - Media format utilities (v210 line length calculation)
 *   - JSON parsing and manipulation for NMOS flow definitions
 *   - Rational number extraction from JSON
 *
 * These utilities enable the GStreamer tools to:
 *   - Parse NMOS flow descriptors from JSON files
 *   - Calculate proper buffer sizes for video formats
 *   - Maintain consistent logging across all tools
 */

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <fmt/format.h>
#include <picojson/picojson.h>
#include "mxl/rational.h"

/**
 * @defgroup LoggingMacros Logging Macros
 * @brief Consistent logging interface for MXL GStreamer tools
 *
 * All macros output to stderr with standardized format: [LEVEL] - message
 * Support fmt::format style format strings with variadic arguments.
 * @{
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

#define MXL_ERROR(msg, ...) MXL_LOG("ERROR", msg __VA_OPT__(,) __VA_ARGS__)  ///< Critical errors
#define MXL_WARN(msg, ...)  MXL_LOG("WARN",  msg __VA_OPT__(,) __VA_ARGS__)  ///< Warnings
#define MXL_INFO(msg, ...)  MXL_LOG("INFO",  msg __VA_OPT__(,) __VA_ARGS__)  ///< Informational
#define MXL_DEBUG(msg, ...) MXL_LOG("DEBUG", msg __VA_OPT__(,) __VA_ARGS__)  ///< Debug details
#define MXL_TRACE(msg, ...) MXL_LOG("TRACE", msg __VA_OPT__(,) __VA_ARGS__)  ///< Verbose trace
// clang-format on
/** @} */

/**
 * @brief Media format utilities for video buffer calculations
 */
namespace media_utils
{
    /**
     * @brief Calculate the byte length of a v210 video line
     *
     * v210 is a 10-bit YUV 4:2:2 packed format commonly used for uncompressed video.
     * Pixels are packed into 32-bit words in groups of 6 pixels (48 bytes per 6 pixels = 128 bits).
     * This function rounds up to the nearest 48-pixel boundary.
     *
     * @param width Frame width in pixels
     * @return Line length in bytes for v210 format
     */
    std::uint32_t getV210LineLength(std::size_t width)
    {
        return static_cast<std::uint32_t>((width + 47) / 48 * 128);
    }
}

/**
 * @brief JSON utilities for NMOS flow definition parsing and manipulation
 *
 * These functions parse NMOS IS-04/IS-05 flow descriptors which define
 * video/audio flow parameters in JSON format.
 */
namespace json_utils
{
    /**
     * @brief Parse a JSON string and return the root object
     *
     * @param jsonBuffer The JSON string to parse
     * @return The root JSON object
     * @throws std::runtime_error if parsing fails or root is not an object
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
     * @brief Parse a JSON file and return the root object
     *
     * @param jsonFile Path to the JSON file
     * @return The root JSON object
     * @throws std::runtime_error if file cannot be opened or parsing fails
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
     * @brief Extract a typed field from a JSON object
     *
     * @tparam T The expected type (e.g., std::string, double, picojson::object)
     * @param obj The JSON object
     * @param name The field name
     * @return The field value
     * @throws std::runtime_error if field is missing or has wrong type
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
     * @brief Extract a typed field from JSON object with default fallback
     *
     * @tparam T The expected type
     * @param obj The JSON object
     * @param name The field name
     * @param default_value Value to return if field is missing or wrong type
     * @return The field value or the default value
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
     * @brief Extract an mxlRational from a JSON field
     *
     * Expects a JSON object with "numerator" and optional "denominator" (defaults to 1).
     * Used for frame rates, sample rates, etc. in NMOS flow descriptors.
     *
     * @param obj The JSON object containing the field
     * @param name The field name (e.g., "grain_rate", "sample_rate")
     * @return The rational value as mxlRational{numerator, denominator}
     * @throws std::runtime_error if field is missing or malformed
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
     * @brief Update the NMOS grouphint tag in a flow descriptor
     *
     * The grouphint tag (urn:x-nmos:tag:grouphint/v1.0) allows flows to be
     * logically grouped together (e.g., video and audio from the same source).
     * Format: "groupName:role" (e.g., "camera-1:video", "camera-1:audio")
     *
     * @param nmosFlow The NMOS flow JSON object (modified in place)
     * @param groupHint The group identifier
     * @param roleInGroup The role within the group (e.g., "video", "audio")
     */
    void updateGroupHint(picojson::object& nmosFlow, std::string const& groupHint, std::string const& roleInGroup)
    {
        auto& tagsObj = nmosFlow.find("tags")->second.get<picojson::object>();
        auto& tagsArray = tagsObj["urn:x-nmos:tag:grouphint/v1.0"].get<picojson::array>();
        tagsArray.clear();
        tagsArray.emplace_back(groupHint + ":" + roleInGroup);
    }

    /**
     * @brief Serialize a JSON object to a compact string
     *
     * @param obj The JSON object to serialize
     * @return Compact JSON string representation (no pretty-printing)
     */
    std::string serializeJson(picojson::object const& obj)
    {
        return picojson::value{obj}.serialize();
    }

} // namespace json_utils
