// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/DiscreteFlowData.hpp"
#include <cstdint>
#include <new>
#include <stdexcept>
#include <fmt/format.h>

namespace mxl::lib
{
    void DiscreteFlowData::openGrainPool(char const* poolFilePath, std::size_t count, std::size_t grainPayloadSize)
    {
        // A grain occupies sizeof(Grain) (the 8 KiB header) followed by its
        // payload, identical to the per-grain file layout. The pool packs
        // `count` such grains back to back at this stride.
        _poolGrainStride = sizeof(Grain) + grainPayloadSize;

        auto const mode = this->created() ? AccessMode::CREATE_READ_WRITE : this->accessMode();
        auto const totalSize = (mode == AccessMode::CREATE_READ_WRITE) ? count * _poolGrainStride : std::size_t{0};

        _pool = SharedMemorySegment{poolFilePath, mode, totalSize, LockMode::Shared};

        if (mode == AccessMode::CREATE_READ_WRITE)
        {
            // Initialize the header of every grain slot in the freshly created pool.
            auto* base = static_cast<std::uint8_t*>(_pool.data());
            for (auto i = std::size_t{0}; i < count; ++i)
            {
                new (base + i * _poolGrainStride) Grain{};
            }
        }
        else
        {
            // Opening an existing pool: recover the stride from the mapped size
            // and validate the grain header version of every slot.
            if (count == 0)
            {
                throw std::invalid_argument{"Cannot open a grain pool with a grain count of zero."};
            }
            _poolGrainStride = _pool.mappedSize() / count;
        }

        _poolGrainCount = count;

        if (mode != AccessMode::CREATE_READ_WRITE)
        {
            for (auto i = std::size_t{0}; i < count; ++i)
            {
                auto const* grain = grainAt(i);
                if ((grain != nullptr) && (grain->header.info.version != GRAIN_HEADER_VERSION))
                {
                    throw std::invalid_argument{
                        fmt::format("Unsupported grain version: {}, supported version is: {}", grain->header.info.version, GRAIN_HEADER_VERSION)};
                }
            }
        }
    }
}
