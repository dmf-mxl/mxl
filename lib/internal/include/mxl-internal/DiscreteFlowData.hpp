// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>
#include <fmt/format.h>
#include "Flow.hpp"
#include "FlowData.hpp"
#include "PayloadStorage.hpp"

namespace mxl::lib
{
    ///
    /// Simple structure holding the shared memory resources of discrete flows.
    ///
    class DiscreteFlowData : public FlowData
    {
    public:
        explicit DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;
        DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

        std::size_t grainCount() const noexcept;

        Grain* emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize);

        Grain* grainAt(std::size_t i) noexcept;
        Grain const* grainAt(std::size_t i) const noexcept;

        mxlGrainInfo* grainInfoAt(std::size_t i) noexcept;
        mxlGrainInfo const* grainInfoAt(std::size_t i) const noexcept;

        /**
         * Set the storage that holds the grain payloads. The flow manager calls this once all grains are mapped.
         * \param[in] storage The payload storage. Replaces any previously set storage.
         */
        void setPayloadStorage(std::unique_ptr<PayloadStorage>&& storage) noexcept;

        /**
         * Accessor for the storage that holds the grain payloads.
         * \return The payload storage.
         * \throws std::logic_error if no storage was set. This is the case for flow data that is only used to
         *      read grain headers, such as the copy held by the domain watcher.
         */
        [[nodiscard]]
        PayloadStorage const& payloadStorage() const;

        /**
         * Compute the storage layout of the grain payloads.
         * \return The storage layout.
         * \throws std::logic_error if no storage was set.
         */
        [[nodiscard]]
        mxlGrainStorageLayout storageLayout() const;

        /**
         * Map the storage of every slot into this process and describe each slot.
         * \param[in] slotCount The number of entries in out_slots. Must be equal to the slot count of the storage.
         * \param[out] out_slots A valid pointer to slotCount structures. Entry i receives the description of slot i.
         * \return MXL_STATUS_OK on success, or MXL_ERR_INVALID_ARG if out_slots is null or slotCount does not match.
         * \throws std::logic_error if no storage was set.
         * \throws std::exception if the storage cannot be mapped in this process.
         */
        mxlStatus mapSlots(std::uint32_t slotCount, mxlGrainStorage* out_slots) const;

    private:
        std::vector<SharedMemoryInstance<Grain>> _grains;
        /**
         * The storage that holds the grain payloads. Declared after _grains so that it is destroyed first, since
         * a storage may refer to the grain segments.
         */
        std::unique_ptr<PayloadStorage> _payloadStorage;
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline DiscreteFlowData::DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : FlowData{std::move(flowSegement)}
        , _grains{}
        , _payloadStorage{}
    {
        _grains.reserve(flowInfo()->config.discrete.grainCount);
    }

    inline DiscreteFlowData::DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
        : FlowData{flowFilePath, mode, lockMode}
        , _grains{}
        , _payloadStorage{}
    {
        _grains.reserve(flowInfo()->config.discrete.grainCount);
    }

    inline std::size_t DiscreteFlowData::grainCount() const noexcept
    {
        return _grains.size();
    }

    inline Grain* DiscreteFlowData::emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize)
    {
        auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();
        auto grain = SharedMemoryInstance<Grain>{grainFilePath, mode, grainPayloadSize, LockMode::Shared};

        if (!this->created())
        {
            // Check for the version of the grain data structure in the memory that was just mapped.
            if (grain.get()->header.info.version != GRAIN_HEADER_VERSION)
            {
                throw std::invalid_argument{
                    fmt::format("Unsupported grain version: {}, supported version is: {}", grain.get()->header.info.version, GRAIN_HEADER_VERSION)};
            }
        }

        return _grains.emplace_back(std::move(grain)).get();
    }

    inline Grain* DiscreteFlowData::grainAt(std::size_t i) noexcept
    {
        return (i < _grains.size()) ? _grains[i].get() : nullptr;
    }

    inline Grain const* DiscreteFlowData::grainAt(std::size_t i) const noexcept
    {
        return (i < _grains.size()) ? _grains[i].get() : nullptr;
    }

    inline mxlGrainInfo* DiscreteFlowData::grainInfoAt(std::size_t i) noexcept
    {
        if (auto const grain = grainAt(i); grain != nullptr)
        {
            return &grain->header.info;
        }
        return nullptr;
    }

    inline mxlGrainInfo const* DiscreteFlowData::grainInfoAt(std::size_t i) const noexcept
    {
        if (auto const grain = grainAt(i); grain != nullptr)
        {
            return &grain->header.info;
        }
        return nullptr;
    }

    inline void DiscreteFlowData::setPayloadStorage(std::unique_ptr<PayloadStorage>&& storage) noexcept
    {
        _payloadStorage = std::move(storage);
    }

    inline PayloadStorage const& DiscreteFlowData::payloadStorage() const
    {
        if (!_payloadStorage)
        {
            throw std::logic_error{"No payload storage set for this flow."};
        }
        return *_payloadStorage;
    }

    inline mxlStatus DiscreteFlowData::mapSlots(std::uint32_t slotCount, mxlGrainStorage* out_slots) const
    {
        auto const& storage = payloadStorage();
        if ((out_slots == nullptr) || (slotCount != storage.slotCount()))
        {
            return MXL_ERR_INVALID_ARG;
        }

        storage.mapSlots();
        for (auto slot = std::uint32_t{0}; slot < slotCount; ++slot)
        {
            out_slots[slot] = storage.slotStorage(slot);
        }
        return MXL_STATUS_OK;
    }

    inline mxlGrainStorageLayout DiscreteFlowData::storageLayout() const
    {
        // All grains of a flow have the same number of slices.
        auto const firstGrain = grainInfoAt(0);
        auto const totalSlices = (firstGrain != nullptr) ? firstGrain->totalSlices : std::uint16_t{0};
        return makeGrainStorageLayout(flowInfo()->config.discrete, totalSlices, payloadStorage());
    }
}
