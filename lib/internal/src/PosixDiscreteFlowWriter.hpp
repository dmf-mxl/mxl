// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/DiscreteFlowData.hpp"
#include "mxl-internal/DiscreteFlowWriter.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Implementation of a FlowWriter based on POSIX shared memory mapping.
     */
    class PosixDiscreteFlowWriter final : public DiscreteFlowWriter
    {
    public:
        /**
         * Creates a PosixDiscreteFlowWriter
         *
         * \param[in] manager A referene to the flow manager used to obtain
         *         additional information about the flows context.
         */
        PosixDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data);

    public:
        /**
         * Accessor for the underlying flow data.
         * The flow writer must first open the flow before invoking this method.
         */
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

        /** \see DiscreteFlowWriter::getGrainInfo */
        [[nodiscard]]
        virtual mxlGrainInfo getGrainInfo(std::uint64_t in_index) const override;

        /** \see DiscreteFlowWriter::openGrain */
        virtual mxlStatus openGrain(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload) override;

        /** \see DiscreteFlowWriter::commit */
        virtual mxlStatus commit(mxlGrainInfo const& mxlGrainInfo) override;

        /** \see DiscreteFlowWriter::cancel */
        virtual mxlStatus cancel() override;

        /** \see FlowWriter::flowRead */
        virtual void flowRead() override;

    private:
        /** The FlowData for the currently opened flow. null if no flow is opened. */
        std::unique_ptr<DiscreteFlowData> _flowData;
        /** The currently opened grain index. MXL_UNDEFINED_INDEX if no grain is currently opened. */
        std::uint64_t _currentIndex;
    };
}
