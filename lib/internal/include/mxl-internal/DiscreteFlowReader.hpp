// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowReader.hpp"

namespace mxl::lib
{
    class MXL_EXPORT DiscreteFlowReader : public FlowReader
    {
    public:
        /**
         * Blocking wait function for a specific grain at a specific index.
         * The index must be greater than or equal to the current tail index of the flow.
         *
         * \param in_index The grain index.
         * \param in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param in_timeoutNs How long to wait in nanoseconds for the grain if in_index is > mxlFlowRuntimeInfo.headIndex
         *
         * \return A status code describing the outcome of the call. Please note
         *      that this method will never return MXL_ERR_TIMEOUT, because the
         *      actual error that is being encountered in this case is
         *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
         *
         * \note Please note that contrary to the various overloads of getGrain, this method does not update the flow access time.
         */
        virtual mxlStatus waitForGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, std::uint64_t in_timeoutNs) const = 0;

        /**
         * Accessor for a specific grain at a specific index.
         * The index must be greater than or equal to the current tail index of the flow.
         *
         * \param in_index The grain index.
         * \param in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param in_timeoutNs How long to wait in nanoseconds for the grain if in_index is > mxlFlowRuntimeInfo.headIndex
         * \param out_grainInfo A valid pointer to mxlGrainInfo that will be copied to
         * \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
         *     Payload size is available in the mxlGrainInfo structure.
         *
         * \return A status code describing the outcome of the call. Please note
         *      that this method will never return MXL_ERR_TIMEOUT, because the
         *      actual error that is being encountered in this case is
         *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
         */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, std::uint64_t in_timeoutNs, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) = 0;

        /**
         * Non-blocking accessor for a specific grain at a specific index.
         * The index must be greater than or equal to the current tail index of the flow.
         *
         * \param in_index The grain index.
         * \param in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param out_grainInfo A valid pointer to mxlGrainInfo that will be copied to
         * \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
         *     Payload size is available in the mxlGrainInfo structure.
         *
         * \return A status code describing the outcome of the call.
         */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) = 0;

    protected:
        using FlowReader::FlowReader;
    };
}
