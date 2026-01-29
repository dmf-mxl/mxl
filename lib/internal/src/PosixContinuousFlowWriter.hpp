// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/ContinuousFlowData.hpp"
#include "mxl-internal/ContinuousFlowWriter.hpp"
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/FlowManager.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Implementation of a FlowWriter based on POSIX shared memory mapping.
     */
    class PosixContinuousFlowWriter final : public ContinuousFlowWriter
    {
    public:
        /**
         * Creates a PosixContinuousFlowWriter
         *
         * \param[in] manager A referene to the flow manager used to obtain
         *          additional information about the flows context.
         */
        PosixContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<ContinuousFlowData>&& data);

    public:
        /** \see FlowWriter::getFlowData */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const override;

        /** \see FlowWriter::getFlowInfo */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const override;

        /** \see FlowWriter::getFlowConfigInfo */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const override;

        /** \see FlowWriter::getFlowRuntimeInfo */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const override;

        /** \see ContinuousFlowWriter::openSamples */
        virtual mxlStatus openSamples(std::uint64_t index, std::size_t count, mxlMutableWrappedMultiBufferSlice& payloadBufferSlices) override;

        /** \see ContinuousFlowWriter::commit */
        virtual mxlStatus commit() override;

        /** \see ContinuousFlowWriter::cancel */
        virtual mxlStatus cancel() override;

        [[nodiscard]]
        virtual bool isExclusive() const override;

        virtual bool makeExclusive() override;

    private:
        bool signalCompletedBatch() noexcept;

    private:
        /** The FlowData for the currently opened flow. null if no flow is opened. */
        std::unique_ptr<ContinuousFlowData> _flowData;
        /** Cached copy of the numer of channels from mxlFlowInfo. */
        std::size_t _channelCount;
        /** Cached copy of the length of the per channel buffers from mxlFlowInfo. */
        std::size_t _bufferLength;
        /** The currently opened sample range head index. MXL_UNDEFINED_INDEX if no range is currently opened. */
        std::uint64_t _currentIndex;

        /** Cached preprocessed copy of mxlCommonFlowInfo::maxSyncBatchSizeHint. */
        std::uint32_t _syncBatchSize;
        /** The threshold at which to signal even before _syncBatchSize is reached, in order to not exceed the maximum. */
        std::uint32_t _earlySyncThreshold;

        /** The last sample batch (as a factor of _syncBatchSize) that has been signaled. */
        std::uint64_t _lastSyncSampleBatch;
    };
}
