#pragma once

#include "FlowWriter.hpp"

namespace mxl::lib
{
    class ContinuousFlowWriter : public FlowWriter
    {
    public:
        /**
         * Accessor for a specific set of mutable samples across all
         * channels starting at a specific index.
         *
         * \param[in] index The starting index of the samples to obtain.
         * \param[in] count The number of samples to obtain.
         * \param[out] payloadBuffersSlices A reference to a mutable
         *      wrapped multi buffer slice that represents the requested
         *      range across all channel buffers
         *
         * \return A status code describing the outcome of the call.
         */
        virtual mxlStatus openSamples(std::uint64_t index, std::size_t count, MutableWrappedMultiBufferSlice& payloadBufferSlices) = 0;

        virtual mxlStatus commit() = 0;

        virtual mxlStatus cancel() = 0;

    protected:
        using FlowWriter::FlowWriter;
    };
}