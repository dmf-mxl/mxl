// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <picojson/wrapper.h>
#include <mxl/platform.h>

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
         * Accessor for the 'grainPool' field, which requests that the discrete flow store all of its grains in a single contiguous shared-memory
         * mapping rather than one mapping per grain. This makes the grains contiguous in virtual memory so an integration can register the whole flow
         * for device DMA with a single mapping. Defaults to false. Ignored for continuous flows.
         */
        [[nodiscard]]
        bool getUseGrainPool() const;

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
        /// Whether to store discrete-flow grains in a single contiguous pool.
        bool _useGrainPool{false};
        /** The parsed flow object. */
        picojson::object _root;
    };

}
