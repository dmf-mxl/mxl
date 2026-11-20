// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <array>
#include <optional>
#include <string>
#include <uuid.h>
#include <picojson/wrapper.h>
#include <mxl/dataformat.h>
#include <mxl/flowinfo.h>
#include <mxl/platform.h>
#include <mxl/rational.h>
#include "GrainGeometry.hpp"

namespace mxl::lib
{
    /**
     * Parses a NMOS Flow resource and extracts / computes key elements based on the flow attributes.
     */
    class MXL_EXPORT FlowParser
    {
    public:
        /**
         * Parses a json flow definition
         *
         * \param in_flowDef The NMOS flow definition
         * \throws std::runtime_error on any parse error
         */
        FlowParser(std::string const& in_flowDef);

        /**
         * Accessor for the 'id' field
         *
         * \return The id field if valid and defined
         */
        [[nodiscard]]
        uuids::uuid const& getId() const;

        /**
         * Accessor for the 'grain_rate' field, which either contains the
         * 'grain rate' for discrete and event flows, or the 'sample rate' for continuous
         * flows.
         *
         * \return The grain rate or sample rate respectively if found and valid.
         */
        [[nodiscard]]
        mxlRational getGrainRate() const;

        /**
         * Accessor for the 'format' field
         *
         * \return The flow format if found and valid
         */
        [[nodiscard]]
        mxlDataFormat getFormat() const;

        /**
         * Accessor for the 'media_type' field.
         *
         * \return The media type.
         * \throws std::invalid_argument if the flow has none.
         */
        [[nodiscard]]
        std::string getMediaType() const;

        /**
         * The number of lines a grain of a video flow holds: the frame height, or half of it for an interlaced flow,
         * whose grains are fields.
         *
         * \return The lines of a grain, or 0 if the flow is not video.
         * \throws std::invalid_argument if the flow is interlaced with an odd frame height.
         */
        [[nodiscard]]
        std::size_t getGrainLines() const;

        /**
         * Computes the geometry of a grain of a discrete flow, for the media types libmxl defines.
         *
         * \return The geometry, or no value if the media type is one libmxl does not define, whose geometry comes from
         *      the payload backend that holds the flow.
         * \throws std::invalid_argument if the flow is not discrete, or the frame size is invalid for the media type.
         */
        [[nodiscard]]
        std::optional<GrainGeometry> getGrainGeometry() const;

        /**
         * Computes the grain payload size
         * \return The payload size
         * \throws std::invalid_argument for a discrete flow whose media type libmxl does not define.
         */
        [[nodiscard]]
        std::size_t getPayloadSize() const;

        /**
         *  Computes the length of the slices of each plane of the grain.
         *  \return The length of slices for each plane.
         *  \throws std::invalid_argument for a discrete flow whose media type libmxl does not define.
         */
        [[nodiscard]]
        std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> getPayloadSliceLengths() const;

        /**
         * Computes the number of slices that make up a full grain.
         * \return The number of slices
         * \throws std::invalid_argument for a discrete flow whose media type libmxl does not define.
         */
        [[nodiscard]]
        std::size_t getTotalPayloadSlices() const;

        /**
         * Get the number of channels in an audio flow.
         * \return The number of channels in an audio flow, or 0
         *      if the flow format indicates that this not an audio flow.
         */
        [[nodiscard]]
        std::size_t getChannelCount() const;

        /**
         * Generic accessor for json fields.
         *
         * \param in_field The field name.
         * \return The field value if found.
         * \throw If the field is not found or T is incompatible
         */
        template<typename T>
        [[nodiscard]]
        T get(std::string const& field) const;

    private:
        /**
         * Computes the geometry of a grain of a video flow, for the media types libmxl defines.
         * \return The geometry, or no value for a media type libmxl does not define.
         * \throws std::invalid_argument if the frame size is invalid for the media type.
         */
        [[nodiscard]]
        std::optional<GrainGeometry> getVideoGrainGeometry() const;

        /**
         * The geometry of a grain of a discrete flow, for the media types libmxl defines.
         * \return The geometry.
         * \throws std::invalid_argument if the flow is not discrete, or its media type is one libmxl does not define.
         */
        [[nodiscard]]
        GrainGeometry requireGrainGeometry() const;

        /** The flow id read from the 'id' field. */
        uuids::uuid _id;
        /** The flow format read from the 'format' field. */
        mxlDataFormat _format;
        /** True if video and interlaced, false otherwise. */
        bool _interlaced;
        /** The flow grain rate, if defined, 0/1 if undefined. */
        mxlRational _grainRate;
        /** The parsed flow object. */
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
