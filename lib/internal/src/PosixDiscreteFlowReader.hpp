// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/DiscreteFlowData.hpp"
#include "mxl-internal/DiscreteFlowReader.hpp"

namespace mxl::lib
{
    class FlowManager;

    /**
     * Implementation of a flow reader based on POSIX shared memory.
     */
    class PosixDiscreteFlowReader final : public DiscreteFlowReader
    {
    public:
        /**
         * \param[in] manager A referene to the flow manager used to obtain
         *         additional information about the flows context.
         */
        PosixDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId, std::unique_ptr<DiscreteFlowData>&& data);

        /**
         * Close the access file fd.
         */
        virtual ~PosixDiscreteFlowReader();

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

        /** \see DiscreteFlowReader::waitForGrain */
        virtual mxlStatus waitForGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline) const override;

        /** \see DiscreteFlowReader::getGrain */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) override;

        /** \see DiscreteFlowReader::getGrain */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) override;

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
         * Implementation of the various forms of getGrain() that can also be
         * used by other methods that have previously asserted that we're
         * operating on a valid flow (i.e. that _flowData is a valid pointer).
         *
         * \param[in] in_index The grain index.
         * \param[in] in_minValidSlices The expected number of valid slices in the grain.
         * \param[out] out_grainInfo If not null, receives a copy of the grain info. Written only on success.
         * \param[out] out_slot If not null, receives the slot that holds the grain. Written only on success.
         * \return A status code describing the outcome of the call.
         */
        mxlStatus getGrainImpl(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo, std::size_t* out_slot) const;

        /**
         * Implementation of the blocking form of getGrain() and waitForGrain()
         * that can also be used by other methods that have previously asserted
         * that we're operating on a valid flow (i.e. that _flowData is a valid
         * pointer).
         *
         * \param[in] in_index The grain index.
         * \param[in] in_minValidSlices The expected number of valid slices in the grain.
         * \param[in] in_deadline The point in time of Clock::Realtime at which to stop waiting.
         * \param[out] out_grainInfo If not null, receives a copy of the grain info. Written only on success.
         * \param[out] out_slot If not null, receives the slot that holds the grain. Written only on success.
         * \return A status code describing the outcome of the call.
         */
        mxlStatus getGrainImpl(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline, mxlGrainInfo* out_grainInfo,
            std::size_t* out_slot) const;

        /**
         * Apply the steps common to all grain reads to the result of a read. On success, update the flow access
         * time. When the grain is too early, check whether the flow is still valid.
         *
         * \param[in] in_result The result of getGrainImpl().
         * \return in_result, or MXL_ERR_FLOW_INVALID if the grain was too early and the flow is no longer valid.
         */
        mxlStatus completeRead(mxlStatus in_result) const;

        /**
         * Host address of the payload of a slot.
         * \param[in] in_slot The slot.
         * \return The host address of the first byte of the slot payload.
         */
        [[nodiscard]]
        std::uint8_t* hostPayloadAt(std::size_t in_slot) const;

    private:
        std::unique_ptr<DiscreteFlowData> _flowData;
        int _accessFileFd;
    };

} // namespace mxl::lib
