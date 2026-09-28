// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowWriter.hpp"

namespace mxl::lib
{
    class MXL_EXPORT DiscreteFlowWriter : public FlowWriter
    {
    public:
        /**
         * Get the grain info for a specific grain index without opening the grain for mutation.
         */
        [[nodiscard]]
        virtual mxlGrainInfo getGrainInfo(std::uint64_t in_index) const = 0;

        virtual mxlStatus openGrain(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

        /**
         * Open a grain for mutation and return its slot in place of a payload pointer. Works for every storage
         * type and does not access the payload storage. Otherwise behaves like openGrain().
         *
         * \param[in] in_index The grain index.
         * \param[out] out_grainInfo A valid pointer that receives a copy of the grain info.
         * \param[out] out_slot A valid pointer that receives the slot holding the grain. Written only on success.
         * \return A status code describing the outcome of the call.
         */
        virtual mxlStatus openGrainSlot(std::uint64_t in_index, mxlGrainInfo* out_grainInfo, std::uint32_t* out_slot) = 0;

        /**
         * Accessor for the storage layout of the grain payloads.
         * \return The storage layout. It does not change for the lifetime of the writer.
         */
        [[nodiscard]]
        virtual mxlGrainStorageLayout getStorageLayout() const = 0;

        /**
         * Map the storage of every slot into this process and describe each slot. See mxlFlowWriterMapSlots().
         * \param[in] in_slotCount The number of entries in out_slots. Must be equal to the slot count of the layout.
         * \param[out] out_slots A valid pointer to in_slotCount structures. Entry i receives the description of slot i.
         * \return MXL_STATUS_OK on success, or MXL_ERR_INVALID_ARG if in_slotCount does not match the layout.
         * \throws std::exception if the storage cannot be mapped in this process.
         */
        virtual mxlStatus mapSlots(std::uint32_t in_slotCount, mxlGrainStorage* out_slots) const = 0;

        virtual mxlStatus commit(mxlGrainInfo const& mxlGrainInfo) = 0;

        virtual mxlStatus cancel() = 0;

    protected:
        using FlowWriter::FlowWriter;
    };
}
