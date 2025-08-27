// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowReader.hpp"

namespace mxl::lib
{

    class DiscreteFlowReader : public FlowReader
    {
    public:
        ///
        /// Accessor for a specific grain at a specific index.
        /// The index must be >= mxlFlowInfo.tailIndex.
        ///
        /// \param in_index The grain index.
        /// \param in_timeoutNs How long to wait in nanoseconds for the grain if in_index is > mxlFlowInfo.headIndex
        /// \param out_grainInfo A valid pointer to mxlGrainInfo that will be copied to
        /// \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
        ///     Payload size is available in the mxlGrainInfo structure.
        ///
        /// \return A status code describing the outcome of the call.
        ///
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint64_t in_timeoutNs, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

        ///
        /// Non-blocking accessor for a specific grain at a specific index.
        /// The index must be >= mxlFlowInfo.tailIndex.
        ///
        /// \param in_index The grain index.
        /// \param out_grainInfo A valid pointer to mxlGrainInfo that will be copied to
        /// \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
        ///     Payload size is available in the mxlGrainInfo structure.
        ///
        /// \return A status code describing the outcome of the call.
        ///
        virtual mxlStatus getGrain(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

    protected:
        using FlowReader::FlowReader;
    };
}