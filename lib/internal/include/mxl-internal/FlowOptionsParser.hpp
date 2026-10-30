// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <picojson/wrapper.h>
#include <mxl/platform.h>
#include "PayloadStorage.hpp"

namespace mxl::lib
{
    /**
     * Parses flow options and extracts valid attributes.
     */
    class MXL_EXPORT FlowOptionsParser
    {
    public:
        FlowOptionsParser() = default;

        /**
         * Parses a json of flow options
         *
         * \param in_flowOptions The flow options
         * \throws std::runtime_error on any parse error
         */
        FlowOptionsParser(std::string const& in_flowOptions);

        /**
         * Accessor for the 'maxCommitBatchSizeHint' field, which expresses the largest expected batch size in samples (for continuous flows) or
         * slices (for discrete flows), in which new data is written to this this flow by its producer. For continuous flows, this value must be less
         * than half of the buffer length. For discrete flows, this must be greater or equal to 1.
         */
        [[nodiscard]]
        std::optional<std::uint32_t> getMaxCommitBatchSizeHint() const;

        /**
         * Accessor for the 'maxSyncBatchSizeHint' field, which expresses the largest expected batch size in samples (for continuous flows) or slices
         * (for discrete flows), at which availability of new data is signaled to waiting consumers. This must be a multiple of the commit batch size
         * greater or equal to 1.
         */
        [[nodiscard]]
        std::optional<std::uint32_t> getMaxSyncBatchSizeHint() const;

        /**
         * The payload storage requested by a writer, from the optional "payload" object of the options.
         *
         * libmxl interprets one key. "backend" names the backend that holds the payload. It defaults to the built-in
         * host storage, which "host" also names. All other keys belong to the backend, for example the device UUID of
         * a CUDA backend, and are passed to it as a JSON object. The built-in host storage accepts no other keys.
         *
         * \return The requested storage.
         * \throws PayloadStorageError with MXL_ERR_INVALID_ARG if the "payload" object is invalid.
         */
        [[nodiscard]]
        PayloadStorageSpec getPayloadStorageSpec() const;

        /**
         * The options a reader passes to the backend of the flow, from the optional "payload" object of the reader
         * options. Readers cannot select the storage, so "backend" is not accepted. All other keys
         * are passed to the backend, which validates them. Flows with the built-in host storage ignore them.
         *
         * \return The options as JSON object text, or an empty string if there are none.
         * \throws PayloadStorageError with MXL_ERR_INVALID_ARG if the "payload" object is invalid.
         */
        [[nodiscard]]
        std::string getReaderPayloadOptions() const;

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
        /// \see mxlCommonFlowInfo::maxSyncBatchSizeHint
        std::optional<std::uint32_t> _maxSyncBatchSizeHint;
        /// \see mxlCommonFlowInfo::maxCommitBatchSizeHint
        std::optional<std::uint32_t> _maxCommitBatchSizeHint;
        /** The parsed flow object. */
        picojson::object _root;
    };

}
