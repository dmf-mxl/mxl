// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>
#include <fmt/format.h>
#include "Flow.hpp"
#include "FlowData.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    ///
    /// Simple structure holding the shared memory resources of discrete flows.
    ///
    /// Grains can be stored in one of two layouts:
    ///  - **Per-grain files** (default): each grain is backed by its own mmap'd
    ///    file (`grains/data.{i}`). The grains are independent mappings and are
    ///    not guaranteed to be contiguous in virtual address space.
    ///  - **Contiguous pool** (opt-in): every grain of the flow lives in a
    ///    single mmap'd file (`grains/pool.data`) at a fixed stride. The grains
    ///    are therefore contiguous in virtual memory, which lets an integration
    ///    register the whole flow for device DMA with a single mapping instead
    ///    of one mapping per grain. See `docs/Fabrics.md` section 11.
    ///
    /// The pool layout is opt-in per flow via the `"grainPool": true` flow
    /// option. Both layouts expose the same `grainAt()` / `grainCount()`
    /// interface, so the rest of the library is agnostic to the storage mode.
    ///
    class DiscreteFlowData : public FlowData
    {
    public:
        explicit DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;
        DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

        std::size_t grainCount() const noexcept;

        /// Add a grain backed by its own file (per-grain file layout).
        Grain* emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize);

        /// Create or open the single backing file that holds every grain of the
        /// flow at a fixed stride (contiguous pool layout).
        ///
        /// \param poolFilePath     Path to the pool file.
        /// \param count            Number of grains in the flow.
        /// \param grainPayloadSize Payload size per grain. Ignored when opening
        ///                         an existing pool (the stride is recovered from
        ///                         the mapped file size).
        void openGrainPool(char const* poolFilePath, std::size_t count, std::size_t grainPayloadSize);

        /// \return true if the grains are stored in the contiguous pool layout.
        bool isPoolMode() const noexcept;

        Grain* grainAt(std::size_t i) noexcept;
        Grain const* grainAt(std::size_t i) const noexcept;

        mxlGrainInfo* grainInfoAt(std::size_t i) noexcept;
        mxlGrainInfo const* grainInfoAt(std::size_t i) const noexcept;

    private:
        // Per-grain file layout (default).
        std::vector<SharedMemoryInstance<Grain>> _grains;

        // Contiguous pool layout (opt-in). Active when _poolGrainCount > 0.
        SharedMemorySegment _pool;
        std::size_t _poolGrainCount{0};
        std::size_t _poolGrainStride{0}; // sizeof(Grain) + grainPayloadSize
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    inline DiscreteFlowData::DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept
        : FlowData{std::move(flowSegement)}
        , _grains{}
    {
        _grains.reserve(flowInfo()->config.discrete.grainCount);
    }

    inline DiscreteFlowData::DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
        : FlowData{flowFilePath, mode, lockMode}
        , _grains{}
    {
        _grains.reserve(flowInfo()->config.discrete.grainCount);
    }

    inline std::size_t DiscreteFlowData::grainCount() const noexcept
    {
        return isPoolMode() ? _poolGrainCount : _grains.size();
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

    inline bool DiscreteFlowData::isPoolMode() const noexcept
    {
        return _poolGrainCount > 0;
    }

    inline Grain* DiscreteFlowData::grainAt(std::size_t i) noexcept
    {
        if (isPoolMode())
        {
            if ((i < _poolGrainCount) && (_pool.data() != nullptr))
            {
                auto* base = static_cast<std::uint8_t*>(_pool.data());
                return reinterpret_cast<Grain*>(base + i * _poolGrainStride);
            }
            return nullptr;
        }
        return (i < _grains.size()) ? _grains[i].get() : nullptr;
    }

    inline Grain const* DiscreteFlowData::grainAt(std::size_t i) const noexcept
    {
        if (isPoolMode())
        {
            if ((i < _poolGrainCount) && (_pool.cdata() != nullptr))
            {
                auto const* base = static_cast<std::uint8_t const*>(_pool.cdata());
                return reinterpret_cast<Grain const*>(base + i * _poolGrainStride);
            }
            return nullptr;
        }
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
}
