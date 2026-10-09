// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <mxl/platform.h>

namespace mxl::lib
{
    ///
    /// A contiguous virtual-address reservation used to lay out several
    /// independently-backed shared-memory mappings back to back.
    ///
    /// The window reserves `count * stride` bytes of address space with a single
    /// anonymous `PROT_NONE` mapping. Callers then overlay each slot with a
    /// file mapping using `MAP_FIXED` (see `SharedMemory`'s fixed-address
    /// constructor). Because the reserved range is owned continuously by this
    /// object, overlaying it with `MAP_FIXED` is race-free and safe.
    ///
    /// The reservation owns the whole address range: it unmaps `[base, base +
    /// count*stride)` in one call on destruction. Mappings overlaid into the
    /// window must therefore be created as externally-owned (they must not unmap
    /// themselves), which is exactly what the fixed-address `SharedMemory`
    /// constructor arranges.
    ///
    /// The purpose is to make several per-grain shared-memory files contiguous
    /// in virtual memory so an integration can register the whole flow for
    /// device DMA / RDMA with a single memory registration, while keeping the
    /// individual `grains/data.{i}` files on disk.
    ///
    class MXL_EXPORT ContiguousWindow
    {
    public:
        /// Construct an invalid (empty) window.
        ContiguousWindow() noexcept = default;

        /// Reserve `count * stride` bytes of contiguous address space.
        ///
        /// \param count  Number of slots.
        /// \param stride Size of each slot in bytes. Must be a multiple of the
        ///               system page size for the overlaid file mappings to be
        ///               correctly page-aligned.
        ///
        /// On failure the window is left in the invalid state (see `valid()`);
        /// no exception is thrown so callers can fall back to a non-contiguous
        /// layout.
        ContiguousWindow(std::size_t count, std::size_t stride) noexcept;

        ~ContiguousWindow();

        ContiguousWindow(ContiguousWindow&& other) noexcept;
        ContiguousWindow& operator=(ContiguousWindow&& other) noexcept;

        ContiguousWindow(ContiguousWindow const&) = delete;
        ContiguousWindow& operator=(ContiguousWindow const&) = delete;

        /// \return true if the reservation is valid.
        [[nodiscard]]
        bool valid() const noexcept
        {
            return _base != nullptr;
        }

        explicit operator bool() const noexcept
        {
            return valid();
        }

        /// \return the target address of slot `i`, or nullptr if invalid.
        [[nodiscard]]
        void* slot(std::size_t i) const noexcept
        {
            return (valid() && (i < _count)) ? static_cast<void*>(_base + (i * _stride)) : nullptr;
        }

        [[nodiscard]]
        std::size_t stride() const noexcept
        {
            return _stride;
        }

        [[nodiscard]]
        std::size_t count() const noexcept
        {
            return _count;
        }

        /// Round `size` up to a multiple of the system page size.
        [[nodiscard]]
        static std::size_t alignToPage(std::size_t size) noexcept;

    private:
        void reset() noexcept;

        std::uint8_t* _base{nullptr};
        std::size_t _bytes{0};
        std::size_t _stride{0};
        std::size_t _count{0};
    };
}
