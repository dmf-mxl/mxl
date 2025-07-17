#pragma once

#include "FlowReader.hpp"

namespace mxl::lib
{

    class DiscreteFlowReader : public FlowReader
    {
    public:
        ///
        /// Accessor for a specific grain at a specific index.
        /// The index must be >= FlowInfo.tailIndex.
        ///
        /// \param in_index The grain index.
        /// \param in_timeoutNs How long to wait in nanoseconds for the grain if in_index is > FlowInfo.headIndex
        /// \param out_grainInfo A valid pointer to GrainInfo that will be copied to
        /// \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
        ///     Payload size is available in the GrainInfo structure.
        ///
        /// \return A status code describing the outcome of the call.
        ///
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint64_t in_timeoutNs, GrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

        ///
        /// Non-blocking accessor for a specific grain at a specific index.
        /// The index must be >= FlowInfo.tailIndex.
        ///
        /// \param in_index The grain index.
        /// \param out_grainInfo A valid pointer to GrainInfo that will be copied to
        /// \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
        ///     Payload size is available in the GrainInfo structure.
        ///
        /// \return A status code describing the outcome of the call.
        ///
        virtual mxlStatus getGrain(std::uint64_t in_index, GrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

    protected:
        using FlowReader::FlowReader;
    };
}