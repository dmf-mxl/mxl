// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "ContinuousFlowData.hpp"
#include "ContinuousFlowWriter.hpp"
#include "FlowManager.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Implementation of a FlowWriter based on POSIX shared memory mapping.
     */
    class PosixContinuousFlowWriter : public ContinuousFlowWriter
    {
    public:
        /**
         * Creates a PosixContinuousFlowWriter
         *
         * \param[in] manager A referene to the flow manager used to obtain
         *          additional information about the flows context.
         */
        PosixContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<ContinuousFlowData>&& data);

        /** \see FlowWriter::getFlowInfo */
        virtual FlowInfo getFlowInfo() override;

        /** \see FlowWriter::getFlowData() */
        virtual FlowData& getFlowData() override;

        /** \see ContinuousFlowWriter::openSamples */
        virtual mxlStatus openSamples(std::uint64_t index, std::size_t count, MutableWrappedMultiBufferSlice& payloadBufferSlices) override;

        /** \see ContinuousFlowWriter::commit */
        virtual mxlStatus commit() override;

        /** \see ContinuousFlowWriter::cancel */
        virtual mxlStatus cancel() override;

        /** \see FlowWriter::flowRead */
        virtual void flowRead() override;

    private:
        /** The FlowData for the currently opened flow. null if no flow is opened. */
        std::unique_ptr<ContinuousFlowData> _flowData;
        /** Cached copy of the numer of channels from FLowInfo. */
        std::size_t _channelCount;
        /** Cached copy of the length of the per channel buffers from FLowInfo. */
        std::size_t _bufferLength;
        /** The currently opened sample range head index. MXL_UNDEFINED_INDEX if no range is currently opened. */
        std::uint64_t _currentIndex;
    };
}
