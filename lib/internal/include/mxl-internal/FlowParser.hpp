// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <array>
#include <string>
#include <uuid.h>
#include <picojson/wrapper.h>
#include <mxl/dataformat.h>
#include <mxl/flowinfo.h>
#include <mxl/platform.h>
#include <mxl/rational.h>

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
         * 'grain rate' for discrete flows, or the 'sample rate' for continuous
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
         * Computes the grain payload size
         * \return The payload size
         */
        [[nodiscard]]
        std::size_t getPayloadSize() const;

        /**
         *  Computes the length of the slices of each plane of the grain.
         *  \return The length of slices for each plane.
         */
        [[nodiscard]]
        std::array<std::uint32_t, MXL_MAX_PLANES_PER_GRAIN> getPayloadSliceLengths() const;

        /**
         * Computes the number of slices that make up a full grain.
         * \return The number of slices
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
