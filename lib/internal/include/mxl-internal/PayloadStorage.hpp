// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <mxl/platform.h>

namespace mxl::lib
{
    class DiscreteFlowData;

    /**
     * Holds the payload of the ring buffer slots of a discrete flow.
     *
     * The grain headers always stay in host shared memory. Only the payload is delegated to a storage object, so
     * that where the payload lives is separate from the ring buffer logic. Readers and writers select a slot by
     * grain index as before and ask the storage for the payload of that slot.
     *
     * Implementations are internal to the SDK. The payload of a slot must not move during the lifetime of the
     * storage object.
     */
    class MXL_EXPORT PayloadStorage
    {
    public:
        /** Destructor. Releases the payload memory owned by the storage, if any. */
        virtual ~PayloadStorage();

        PayloadStorage(PayloadStorage const&) = delete;
        PayloadStorage(PayloadStorage&&) = delete;
        PayloadStorage& operator=(PayloadStorage const&) = delete;
        PayloadStorage& operator=(PayloadStorage&&) = delete;

        /**
         * The number of slots in the storage.
         * \return The number of slots. Equal to the grain count of the flow.
         */
        [[nodiscard]]
        virtual std::size_t slotCount() const noexcept = 0;

        /**
         * Host address of the payload of one slot.
         * \param[in] slot The slot. Must be less than slotCount().
         * \return The host address of the first payload byte of the slot.
         * \throws std::out_of_range if slot is not less than slotCount().
         */
        [[nodiscard]]
        virtual std::uint8_t* hostPayload(std::size_t slot) const = 0;

    protected:
        /** Default constructor for implementations. */
        PayloadStorage() = default;
    };

    /**
     * Payload storage in host shared memory, as used by all flows so far.
     *
     * The payload of each slot directly follows its grain header in the shared memory segment of the grain.
     * The storage does not own that memory. The segments belong to the DiscreteFlowData the storage was built
     * from, which must outlive the storage.
     */
    class MXL_EXPORT
    HostPayloadStorage final : public PayloadStorage
    {
    public:
        /**
         * Build the storage from the grains of a discrete flow.
         * \param[in] flowData The flow data whose grains hold the payload. All grains must already be mapped.
         */
        explicit HostPayloadStorage(DiscreteFlowData& flowData);

        /** \see PayloadStorage::slotCount() */
        [[nodiscard]]
        std::size_t slotCount() const noexcept override;

        /** \see PayloadStorage::hostPayload() */
        [[nodiscard]]
        std::uint8_t* hostPayload(std::size_t slot) const override;

    private:
        /** Host address of the payload of each slot, indexed by slot. */
        std::vector<std::uint8_t*> _slotPayloads;
    };
}
