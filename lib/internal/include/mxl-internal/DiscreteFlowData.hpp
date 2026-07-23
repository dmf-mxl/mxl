// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <limits>
#include <vector>
#include <fmt/format.h>
#include "ContiguousWindow.hpp"
#include "Flow.hpp"
#include "FlowData.hpp"
#include "SharedMemory.hpp"

namespace mxl::lib
{
    ///
    /// Simple structure holding the shared memory resources of discrete flows.
    ///
    /// Grains are always backed by one mmap'd file per grain (`grains/data.{i}`)
    /// and can be stored in one of two layouts:
    ///  - **Per-grain files** (default): each grain is an independent mapping
    ///    placed at a kernel-chosen address, so the grains are not guaranteed to
    ///    be contiguous in virtual address space.
    ///  - **Contiguous per-grain files** (opt-in): each grain keeps its own
    ///    file, but every grain is mapped at a fixed address inside a single
    ///    reserved address window (see `ContiguousWindow`). The grains are
    ///    therefore contiguous in virtual memory, which lets an integration
    ///    register the whole flow for device DMA / RDMA with a single mapping
    ///    instead of one mapping per grain, while keeping the per-grain files on
    ///    disk. See `docs/ContiguousGrainMapping.md`.
    ///
    /// The contiguous layout is opt-in per flow via the `"contiguousGrains":
    /// true` flow option. Both layouts expose the same `grainAt()` /
    /// `grainCount()` interface, so the rest of the library is agnostic to the
    /// storage mode.
    ///
    class DiscreteFlowData : public FlowData
    {
    public:
        explicit DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;
        DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

        std::size_t grainCount() const noexcept;

        /// Add a grain backed by its own file (per-grain file layout).
        Grain* emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize);

        /// Reserve a contiguous address window for `count` grains, each of
        /// `grainStride` bytes (which must be page-aligned, see
        /// `grainStride()`). Grains added afterwards with `emplaceGrainAt()` are
        /// mapped into this window so they are contiguous in virtual memory
        /// while remaining backed by individual per-grain files.
        ///
        /// \return true if the window was reserved. On failure the caller
        ///         should fall back to `emplaceGrain()` (non-contiguous).
        bool openContiguousWindow(std::size_t count, std::size_t grainStride) noexcept;

        /// Add a grain backed by its own file, mapped at slot `i` of the
        /// contiguous window previously reserved with `openContiguousWindow()`.
        Grain* emplaceGrainAt(std::size_t i, char const* grainFilePath, std::size_t grainPayloadSize);

        /// Close all grains and release the contiguous address window.
        void resetContiguousWindow() noexcept;

        /// \return true if the grains are stored as contiguous per-grain files.
        bool isWindowMode() const noexcept;

        /// \return the mapped size of grain `i`, or zero for an invalid index.
        std::size_t grainMappedSize(std::size_t i) const noexcept;

        /// \return the actual slot stride of the contiguous window, or zero.
        std::size_t contiguousWindowStride() const noexcept;

        /// The page-aligned stride of a grain slot for the given payload size,
        /// i.e. the size a single grain occupies inside a contiguous window.
        static std::size_t grainStride(std::size_t grainPayloadSize) noexcept;

        Grain* grainAt(std::size_t i) noexcept;
        Grain const* grainAt(std::size_t i) const noexcept;

        mxlGrainInfo* grainInfoAt(std::size_t i) noexcept;
        mxlGrainInfo const* grainInfoAt(std::size_t i) const noexcept;

    private:
        // Per-grain file layout (default and contiguous-window layouts).
        std::vector<SharedMemoryInstance<Grain>> _grains;

        // Contiguous per-grain-file layout (opt-in). When valid, the grains in
        // `_grains` are mapped at fixed addresses inside this reservation.
        ContiguousWindow _window;
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

    inline bool DiscreteFlowData::isWindowMode() const noexcept
    {
        return _window.valid();
    }

    inline std::size_t DiscreteFlowData::grainMappedSize(std::size_t i) const noexcept
    {
        return (i < _grains.size()) ? _grains[i].mappedSize() : 0U;
    }

    inline std::size_t DiscreteFlowData::contiguousWindowStride() const noexcept
    {
        return _window.stride();
    }

    inline std::size_t DiscreteFlowData::grainStride(std::size_t grainPayloadSize) noexcept
    {
        if (grainPayloadSize > (std::numeric_limits<std::size_t>::max() - sizeof(Grain)))
        {
            return 0U;
        }
        return ContiguousWindow::alignToPage(sizeof(Grain) + grainPayloadSize);
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
}
