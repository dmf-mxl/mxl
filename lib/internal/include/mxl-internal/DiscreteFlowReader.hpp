// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowReader.hpp"
#include "Timing.hpp"

namespace mxl::lib
{
    class MXL_EXPORT DiscreteFlowReader : public FlowReader
    {
    public:
        /**
         * Blocking wait function for a specific grain at a specific index.
         * The index must be greater than or equal to the current tail index of the flow.
         *
         * \param in_index The grain index.
         * \param in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param in_deadline The point in time of Clock::Realtime at which to stop waiting.
         *
         * \return A status code describing the outcome of the call. Please note
         *      that this method will never return MXL_ERR_TIMEOUT, because the
         *      actual error that is being encountered in this case is
         *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
         *
         * \note Please note that contrary to the various overloads of getGrain, this method does not update the flow access time.
         */
        virtual mxlStatus waitForGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline) const = 0;

        /**
         * Accessor for a specific grain at a specific index.
         * The index must be greater than or equal to the current tail index of the flow.
         *
         * \param in_index The grain index.
         * \param in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param in_deadline The point in time of Clock::Realtime at which to stop waiting.
         * \param out_grainInfo A valid pointer to mxlGrainInfo that will be copied to
         * \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
         *     Payload size is available in the mxlGrainInfo structure.
         *
         * \return A status code describing the outcome of the call. Please note
         *      that this method will never return MXL_ERR_TIMEOUT, because the
         *      actual error that is being encountered in this case is
         *      MXL_ERR_OUT_OF_RANGE_TOO_EARLY, even after waiting.
         */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) = 0;

        /**
         * Non-blocking accessor for a specific grain at a specific index.
         * The index must be greater than or equal to the current tail index of the flow.
         *
         * \param in_index The grain index.
         * \param in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param out_grainInfo A valid pointer to mxlGrainInfo that will be copied to
         * \param out_payload A valid void pointer to pointer that will be set to the first byte of the grain payload.
         *     Payload size is available in the mxlGrainInfo structure.
         *
         * \return A status code describing the outcome of the call.
         */
        virtual mxlStatus getGrain(std::uint64_t in_index, std::uint16_t in_minValidSlices, mxlGrainInfo* out_grainInfo,
            std::uint8_t** out_payload) = 0;

        /**
         * Blocking accessor for a specific grain that returns its slot in place of a payload pointer. Works for
         * every storage type and does not access the payload storage. Otherwise behaves like the blocking getGrain().
         *
         * \param[in] in_index The grain index.
         * \param[in] in_minValidSlices The expected number of valid slices in the returned mxlGrainInfo.
         * \param[in] in_deadline The point in time of Clock::Realtime at which to stop waiting. A deadline in the
         *      past makes the call non-blocking.
         * \param[out] out_grainInfo A valid pointer to mxlGrainInfo that will be copied to.
         * \param[out] out_slot A valid pointer that receives the slot holding the grain. Written only if the call
         *      succeeds.
         *
         * \return A status code describing the outcome of the call. Never MXL_ERR_TIMEOUT, see getGrain().
         */
        virtual mxlStatus getGrainSlot(std::uint64_t in_index, std::uint16_t in_minValidSlices, Timepoint in_deadline, mxlGrainInfo* out_grainInfo,
            std::uint32_t* out_slot) = 0;

        /**
         * Accessor for the storage layout of the grain payloads.
         * \return The storage layout. It does not change for the lifetime of the reader.
         */
        [[nodiscard]]
        virtual mxlGrainStorageLayout getStorageLayout() const = 0;

        /**
         * Map the storage of every slot into this process and describe each slot. See mxlFlowReaderMapSlots().
         * \param[in] in_slotCount The number of entries in out_slots. Must be equal to the slot count of the layout.
         * \param[out] out_slots A valid pointer to in_slotCount structures. Entry i receives the description of slot i.
         * \return MXL_STATUS_OK on success, or MXL_ERR_INVALID_ARG if in_slotCount does not match the layout.
         * \throws std::exception if the storage cannot be mapped in this process.
         */
        virtual mxlStatus mapSlots(std::uint32_t in_slotCount, mxlGrainStorage* out_slots) const = 0;

    protected:
        using FlowReader::FlowReader;
    };
}
