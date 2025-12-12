// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/ContinuousFlowData.hpp"
#include "mxl-internal/ContinuousFlowReader.hpp"
#include "mxl-internal/FlowManager.hpp"

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

        /** \see FlowReader::getFlowData */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const override;

    public:
        /** \see FlowReader::getFlowInfo */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const override;

        /** \see FlowReader::getFlowConfigInfo */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const override;

        /** \see FlowReader::getFlowRuntimeInfo */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const override;

        /** \see ContinuousFlowReader::waitForSamples */
        virtual mxlStatus waitForSamples(std::uint64_t index, Timepoint deadline) const override;

        /** \see ContinuousFlowReader::getSamples */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, Timepoint deadline,
            mxlWrappedMultiBufferSlice& payloadBuffersSlices) override;

        /** \see ContinuousFlowReader::getSamples */
        virtual mxlStatus getSamples(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice& payloadBuffersSlices) override;

    protected:
        /** \see FlowReader::isFlowValid */
        [[nodiscard]]
        virtual bool isFlowValid() const override;

    private:
        /**
         * Implementation of isFlowValid() that can be used by other methods
         * that have previously asserted that we're operating on a valid flow
         * (i.e. that _flowData is a valid pointer).
         */
        [[nodiscard]]
        bool isFlowValidImpl() const;

        /**
         * Implementation of the various forms of getSamples() that can also be
         * used by other methods that have previously asserted that we're
         * operating on a valid flow (i.e. that _flowData is a valid pointer).
         */
        mxlStatus getSamplesImpl(std::uint64_t index, std::size_t count, mxlWrappedMultiBufferSlice* payloadBuffersSlices) const;

        /**
         * Implementation of the blocking form of getSamples() and waitForSamples()
         * that can also be used by other methods that have previously asserted
         * that we're operating on a valid flow (i.e. that _flowData is a valid
         * pointer).
         */
        mxlStatus getSamplesImpl(std::uint64_t index, std::size_t count, Timepoint deadline, mxlWrappedMultiBufferSlice* payloadBuffersSlices) const;

    private:
        std::unique_ptr<ContinuousFlowData> _flowData;
        /** Cached copy of the numer of channels from mxlFlowInfo. */
        std::size_t _channelCount;
        /** Cached copy of the length of the per channel buffers from mxlFlowInfo. */
        std::size_t _bufferLength;
    };
}
