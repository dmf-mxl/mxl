// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file DiscreteFlowData.hpp
 * @brief FlowData specialization for discrete flows (VIDEO/DATA)
 *
 * Discrete flows exchange individual "grains" (frames, packets) via a ring buffer.
 *
 * ARCHITECTURE:
 *
 *   Filesystem layout:
 *     ${domain}/${flowId}.mxl-flow/
 *       data               -- DiscreteFlowState (ring buffer metadata)
 *       grains/
 *         data.0           -- Grain file (header + payload)
 *         data.1           -- Grain file
 *         ...
 *         data.N-1         -- Grain file (N = grain count)
 *
 *   Ring buffer semantics:
 *     - Fixed number of grain slots (e.g., 16 grains)
 *     - Each grain is a separate memory-mapped file
 *     - Writer fills grains in order (0, 1, 2, ..., N-1, 0, ...)
 *     - Ring buffer index = grainIndex % grainCount
 *     - Oldest grain is overwritten when buffer wraps
 *
 *   Why separate grain files?
 *     - Partial grain writes (slice-by-slice for video)
 *     - Lazy allocation (grains mapped on-demand)
 *     - Flexible per-grain sizes (though typically uniform)
 *     - Easier debugging (can examine individual grain files)
 *
 * KEY DESIGN DECISIONS:
 *
 * 1. Grain mapping is lazy:
 *    - Not all grains are mapped initially
 *    - emplaceGrain() maps grain on first access
 *    - Readers only map grains they actually read
 *
 * 2. Versioning:
 *    - Each grain header has a version number
 *    - Reader checks version matches GRAIN_HEADER_VERSION
 *    - Protects against format mismatches
 *
 * 3. Payload offset:
 *    - Grain payload starts at offset 8192 (MXL_GRAIN_PAYLOAD_OFFSET)
 *    - Header is fixed size with padding
 *    - Enables page-aligned, AVX-512-aligned payload
 */

#pragma once

#include <vector>
#include <fmt/format.h>
#include "Flow.hpp"
#include "FlowData.hpp"

namespace mxl::lib
{
    /**
     * FlowData specialization for discrete flows (VIDEO/DATA).
     *
     * Manages:
     * - Flow "data" file mapping (DiscreteFlowState)
     * - Vector of grain file mappings (lazy)
     * - Grain ring buffer indexing
     *
     * Used by both DiscreteFlowReader and DiscreteFlowWriter.
     */
    class DiscreteFlowData : public FlowData
    {
    public:
        /**
         * Construct from an existing Flow mapping.
         * Used when caller has already opened the "data" file.
         * @param flowSegement Existing SharedMemoryInstance<Flow> (moved)
         */
        explicit DiscreteFlowData(SharedMemoryInstance<Flow>&& flowSegement) noexcept;

        /**
         * Construct by opening the "data" file.
         * @param flowFilePath Path to ${domain}/${flowId}.mxl-flow/data
         * @param mode READ_ONLY (reader) or READ_WRITE/CREATE_READ_WRITE (writer)
         * @param lockMode Advisory lock mode (Shared for readers, Exclusive for writers)
         * @throws std::runtime_error if open/map fails
         */
        DiscreteFlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode);

        /**
         * Get the number of currently mapped grains.
         * Not necessarily equal to the ring buffer size (grains are mapped lazily).
         * @return Number of grains in _grains vector
         */
        std::size_t grainCount() const noexcept;

        /**
         * Map a grain file and add it to the grain vector.
         *
         * Called by:
         * - FlowWriter during grain creation (mode == CREATE_READ_WRITE)
         * - FlowReader when accessing a grain for the first time (mode == READ_ONLY)
         *
         * Steps:
         * 1. Determine mode (create if writer created flow, otherwise use accessMode)
         * 2. Map grain file with SharedMemoryInstance<Grain>
         * 3. If opening existing grain, validate GRAIN_HEADER_VERSION
         * 4. Add to _grains vector and return pointer
         *
         * @param grainFilePath Path to grain file (e.g., ${domain}/${flowId}.mxl-flow/grains/data.0)
         * @param grainPayloadSize Payload size in bytes (excluding 8192-byte header)
         * @return Pointer to mapped Grain structure
         * @throws std::invalid_argument if version mismatch
         * @throws std::runtime_error if map fails
         */
        Grain* emplaceGrain(char const* grainFilePath, std::size_t grainPayloadSize);

        /**
         * Get pointer to grain at index i in the _grains vector (not grain ring buffer index).
         * @param i Index in _grains vector (0 to grainCount()-1)
         * @return Pointer to Grain, or nullptr if i out of range
         */
        Grain* grainAt(std::size_t i) noexcept;

        /**
         * Get const pointer to grain at index i in the _grains vector.
         * @param i Index in _grains vector (0 to grainCount()-1)
         * @return Const pointer to Grain, or nullptr if i out of range
         */
        Grain const* grainAt(std::size_t i) const noexcept;

        /**
         * Get pointer to mxlGrainInfo at index i (convenience accessor).
         * @param i Index in _grains vector
         * @return Pointer to mxlGrainInfo within grain header, or nullptr if i out of range
         */
        mxlGrainInfo* grainInfoAt(std::size_t i) noexcept;

        /**
         * Get const pointer to mxlGrainInfo at index i.
         * @param i Index in _grains vector
         * @return Const pointer to mxlGrainInfo within grain header, or nullptr if i out of range
         */
        mxlGrainInfo const* grainInfoAt(std::size_t i) const noexcept;

    private:
        /**
         * Vector of mapped grain files.
         *
         * Indexing: _grains[i] is the i-th grain in the ring buffer (0 to N-1)
         * Mapping: Grains are mapped lazily (vector grows as grains are accessed)
         * Capacity: Pre-reserved to flowInfo()->config.discrete.grainCount (avoid reallocs)
         *
         * Memory: Each entry is SharedMemoryInstance<Grain> (~48 bytes) + mmap
         * Lifecycle: Grains unmapped when vector destroyed (RAII)
         */
        std::vector<SharedMemoryInstance<Grain>> _grains;
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
        // Determine mode: If we created the flow, create grains. Otherwise, use flow's access mode (read-only for readers).
        auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();

        // Map the grain file (header + payload). If creating, file is truncated to size.
        // Shared lock: Multiple readers can access same grain, writer holds shared lock per grain.
        auto grain = SharedMemoryInstance<Grain>{grainFilePath, mode, grainPayloadSize, LockMode::Shared};

        // If opening existing grain (not creating), validate the version number
        if (!this->created())
        {
            // Check for the version of the grain data structure in the memory that was just mapped.
            // Protects against reading grains written by incompatible MXL versions.
            if (grain.get()->header.info.version != GRAIN_HEADER_VERSION)
            {
                throw std::invalid_argument{
                    fmt::format("Unsupported grain version: {}, supported version is: {}", grain.get()->header.info.version, GRAIN_HEADER_VERSION)};
            }
        }

        // Add grain to vector and return pointer (emplace_back returns reference, get() extracts pointer)
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
}
