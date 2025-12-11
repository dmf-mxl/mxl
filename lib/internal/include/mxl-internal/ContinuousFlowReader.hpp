// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowReader.hpp"

namespace mxl::lib
{
    class MXL_EXPORT ContinuousFlowReader : public FlowReader
    {
    public:
        /**
         * Blocking wait function for the sample at the specified index to
         * become available.
         *
         * \param[in] index The starting index of the samples to obtain.
         * \param[in] timeoutNs How long to wait in nanoseconds for the range
         *      of samples to become available.
         *
         * \return A status code describing the outcome of the call. Please note
         *      that this method will never return MXL_ERR_TIMEOUT, because the
         *      actual error that is being encountered in this case is
         *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
         * \note No guarantees are made as to how long the caller may
         *      safely hang on to the returned range of samples without the
         *      risk of these samples being overwritten.
         */
        virtual mxlStatus waitForSamples(std::uint64_t index, std::uint64_t timeoutNs) const = 0;

        /**
         * Accessor for a specific set of samples across all channels
         * ending at a specific index (`count` samples up to `index`).
         *
         * \param[in] index The starting index of the samples to obtain.
         * \param[in] count The number of samples to obtain.
         * \param[in] timeoutNs How long to wait in nanoseconds for the range
         *      of samples to become available.
         * \param[out] payloadBuffersSlices A reference to a wrapped multi
         *      buffer slice that represents the requested range across all
         *      channel buffers
         *
         * \return A status code describing the outcome of the call. Please note
         *      that this method will never return MXL_ERR_TIMEOUT, because the
         *      actual error that is being encountered in this case is
         *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
         * \note No guarantees are made as to how long the caller may
         *      safely hang on to the returned range of samples without the
         *      risk of these samples being overwritten.
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, std::uint64_t timeoutNs,
            mxlWrappedMultiBufferSlice& payloadBufferSlices) = 0;

        /**
         * Non-blocking accessor for a specific set of samples across all
         * channels ending at a specific index (`count` samples up to `index`).
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
