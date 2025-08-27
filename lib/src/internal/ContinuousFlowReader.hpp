// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowReader.hpp"

namespace mxl::lib
{
    class ContinuousFlowReader : public FlowReader
    {
    public:
        /**
         * Accessor for a specific set of samples across all channels
         * ending at a specific index (`count` samples up to `index`).
         *
         * \param[in] index The starting index of the samples to obtain.
         * \param[in] count The number of samples to obtain.
         * \param[out] payloadBuffersSlices A reference to a wrapped multi
         *      buffer slice that represents the requested range across all
         *      channel buffers
         *
         * \return A status code describing the outcome of the call.
         * \note No guarantees are made as to how long the caller may
         *      safely hang on to the returned range of samples without the
         *      risk of these samples being overwritten.
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBufferSlices) = 0;

    protected:
        using FlowReader::FlowReader;
    };
}