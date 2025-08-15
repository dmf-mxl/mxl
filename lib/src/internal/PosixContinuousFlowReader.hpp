// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "ContinuousFlowData.hpp"
#include "ContinuousFlowReader.hpp"
#include "FlowManager.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Implementation of a continuous flow reader based on POSIX shared memory.
     */
    class PosixContinuousFlowReader final : public ContinuousFlowReader
    {
    public:
        /**
         * \param[in] manager A referene to the flow manager used to obtain
         *      additional information about the flows context.
         */
        PosixContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<ContinuousFlowData>&& data);

        /**
         * Accessor for the current FlowInfo. A copy of the current structure is returned.
         * The reader must be properly attached to the flow before invoking this method.
         * \return A copy of the FlowInfo
         */
        virtual FlowInfo getFlowInfo() override;

        /**
         * Accessor for the underlying flow data.
         * The reader must be properly attached to the flow before invoking this method.
         * \return A copy of the FlowInfo
         */
        virtual FlowData& getFlowData() override;

        /**
         * Accessor for a specific set of samples across all channels
         * ending at a specific index (`count` samples up to `index`).
         *
         * \param[in] index The head index of the samples to obtain.
         * \param[in] count The number of samples to obtain.
         * \param[out] payloadBuffersSlices A reference to a wrapped multi
         *      buffer slice that represents the requested range across all
         *      channel buffers.
         *
         * \return A status code describing the outcome of the call.
         * \note No guarantees are made as to how long the caller may
         *      safely hang on to the returned range of samples without the
         *      risk of these samples being overwritten.
         */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, WrappedMultiBufferSlice& payloadBuffersSlices) override;

    private:
        std::unique_ptr<ContinuousFlowData> _flowData;
        /** Cached copy of the numer of channels from FLowInfo. */
        std::size_t _channelCount;
        /** Cached copy of the length of the per channel buffers from FLowInfo. */
        std::size_t _bufferLength;
    };
}
