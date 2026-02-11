// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file FlowParser.hpp
 * @brief Parse NMOS IS-04 Flow definitions into MXL internal structures
 *
 * MXL uses NMOS IS-04 Flow resources (JSON) to describe media flows.
 * Example NMOS flow definition:
 * {
 *   "id": "abc-123-def-456",
 *   "format": "urn:x-nmos:format:video",
 *   "grain_rate": {"numerator": 24000, "denominator": 1001},
 *   "frame_width": 1920,
 *   "frame_height": 1080,
 *   "interlace_mode": "progressive",
 *   "components": [...],
 *   ...
 * }
 *
 * FlowParser extracts:
 * - Flow UUID (from "id")
 * - Data format (VIDEO_V210, AUDIO_F32, etc.) derived from "format" and "components"
 * - Grain rate (frame rate for video, sample rate for audio)
 * - Dimensions, interlacing, color space (for video)
 * - Channel count (for audio)
 * - Computed payload sizes (grains, slices, samples)
 *
 * Why NMOS?
 * - Industry-standard media flow description (AMWA IS-04)
 * - Already supported by broadcast equipment and software
 * - Rich metadata (timing, colorimetry, audio channels)
 * - Enables interoperability with NMOS-based systems
 *
 * Design choice:
 * - Parse once in constructor, cache results (parser is immutable)
 * - Throws on invalid JSON or missing required fields (fail-fast)
 * - Uses picojson for JSON parsing (lightweight, header-only)
 */

#pragma once

#include <cstddef>
#include <array>
#include <string>
#include <uuid.h>
#include <picojson/picojson.h>
#include <mxl/dataformat.h>
#include <mxl/flowinfo.h>
#include <mxl/platform.h>
#include <mxl/rational.h>

namespace mxl::lib
{
    /**
     * Parses a NMOS Flow resource and extracts/computes key elements based on the flow attributes.
     *
     * Thread-safety: Immutable after construction (thread-safe for reads).
     */
    class MXL_EXPORT FlowParser
    {
    public:
        /**
         * Parse a JSON NMOS flow definition.
         *
         * Extracts and validates:
         * - Flow ID (UUID)
         * - Format (video/audio/data)
         * - Grain rate (frame rate or sample rate)
         * - Format-specific parameters (resolution, color space, channels, etc.)
         *
         * @param in_flowDef JSON string conforming to NMOS IS-04 Flow schema
         * @throws std::runtime_error if JSON is malformed or required fields are missing
         * @throws std::invalid_argument if values are out of range or incompatible
         *
         * Example valid input:
         *   {"id": "...", "format": "urn:x-nmos:format:video", "grain_rate": {"numerator": 30000, "denominator": 1001}, ...}
         */
        FlowParser(std::string const& in_flowDef);

        /**
         * Get the flow's UUID (from "id" field).
         * @return Flow UUID
         */
        [[nodiscard]]
        uuids::uuid const& getId() const;

        /**
         * Get the grain rate (frame rate for video, sample rate for audio).
         *
         * For discrete flows (VIDEO/DATA): "grain_rate" field
         * For continuous flows (AUDIO): "sample_rate" field
         *
         * Examples:
         * - Video: {24000, 1001} = 23.976 fps
         * - Audio: {48000, 1} = 48 kHz
         *
         * @return Grain/sample rate as rational (numerator/denominator)
         */
        [[nodiscard]]
        mxlRational getGrainRate() const;

        /**
         * Get the data format (VIDEO_V210, AUDIO_F32, DATA_ST291, etc.).
         * Derived from NMOS "format" and "components" fields.
         *
         * @return MXL data format enum
         */
        [[nodiscard]]
        mxlDataFormat getFormat() const;

        /**
         * Compute the payload size in bytes for one grain (video/data) or sample buffer (audio).
         *
         * For video:
         * - V210: height * getV210LineLength(width)
         * - Includes padding per SMPTE specs
         *
         * For audio:
         * - F32: channels * samples_per_period * sizeof(float)
         *
         * For ancillary:
         * - ST 291: variable-length packets (returns max packet size)
         *
         * @return Payload size in bytes
         */
        [[nodiscard]]
        std::size_t getPayloadSize() const;

        /**
         * Get the length of each slice (scan line) per plane.
         *
         * For video, a slice is typically one scan line (horizontal row of pixels).
         * For planar formats, each plane may have different slice lengths.
         *
         * Example V210 1920x1080:
         *   plane[0] = 5120 bytes/line (Y+CbCr packed)
         *   plane[1] = 0 (no separate planes for packed format)
         *
         * @return Array of slice lengths per plane (in bytes)
         */
        [[nodiscard]]
        std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> getPayloadSliceLengths() const;

        /**
         * Get the total number of slices per grain (sum across all planes).
         *
         * For 1920x1080 progressive: 1080 slices (one per scan line)
         * For 1920x1080 interlaced: 540 slices per field
         *
         * Used for partial grain writes (slice-by-slice commitment).
         *
         * @return Total slice count
         */
        [[nodiscard]]
        std::size_t getTotalPayloadSlices() const;

        /**
         * Get the number of audio channels (for audio flows only).
         * @return Channel count, or 0 if not an audio flow
         *
         * Examples: 2 (stereo), 8 (7.1 surround), 16 (Dante multi-channel)
         */
        [[nodiscard]]
        std::size_t getChannelCount() const;

        /**
         * Generic accessor for arbitrary JSON fields.
         *
         * Allows extracting custom fields not explicitly handled by this parser.
         * Type T must match the JSON value type (string, int64_t, double, bool).
         *
         * @tparam T Expected C++ type (automatically deduced from picojson)
         * @param field JSON field name
         * @return Field value cast to T
         * @throws std::invalid_argument if field not found or type mismatch
         *
         * Example: auto label = parser.get<std::string>("label");
         */
        template<typename T>
        [[nodiscard]]
        T get(std::string const& field) const;

    private:
        /**
         * Flow UUID extracted from "id" field.
         * Used for flow directory naming (${uuid}.mxl-flow) and identification.
         */
        uuids::uuid _id;

        /**
         * MXL data format derived from NMOS "format" and "components".
         * Maps NMOS colorspace/sampling/depth to MXL internal enum.
         */
        mxlDataFormat _format;

        /**
         * Interlacing flag (video only).
         * True if "interlace_mode" is "interlaced_tff" or "interlaced_bff".
         * Affects slice count calculation (fields vs. frames).
         */
        bool _interlaced;

        /**
         * Grain rate (frame rate or sample rate) as rational.
         * Extracted from "grain_rate" (video/data) or "sample_rate" (audio).
         * Defaults to {0, 1} if undefined (invalid/uninitialized).
         */
        mxlRational _grainRate;

        /**
         * Parsed JSON object (picojson).
         * Retained for generic field access via get<T>() template.
         * Contains full NMOS flow definition.
         */
        picojson::object _root;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    template<typename T>
    inline T FlowParser::get(std::string const& field) const
    {
        if (auto const it = _root.find(field); it != _root.end())
        {
            return it->second.get<T>();
        }
        else
        {
            auto msg = std::string{"Required '"} + field + "' not found.";
            throw std::invalid_argument{std::move(msg)};
        }
    }

}
