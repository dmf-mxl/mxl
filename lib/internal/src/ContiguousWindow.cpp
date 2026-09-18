// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/ContiguousWindow.hpp"
#include <limits>
#include <utility>
#include <unistd.h>
#include <sys/mman.h>
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    std::size_t ContiguousWindow::alignToPage(std::size_t size) noexcept
    {
        auto const systemPageSize = ::sysconf(_SC_PAGESIZE);
        if (systemPageSize <= 0)
        {
            return 0U;
        }

        auto const pageSize = static_cast<std::size_t>(systemPageSize);
        if (size > (std::numeric_limits<std::size_t>::max() - (pageSize - 1U)))
        {
            return 0U;
        }
        return ((size + pageSize - 1U) / pageSize) * pageSize;
    }

    ContiguousWindow::ContiguousWindow(std::size_t count, std::size_t stride) noexcept
    {
        if ((count == 0U) || (stride == 0U))
        {
            return;
        }

        if (count > (std::numeric_limits<std::size_t>::max() / stride))
        {
            return;
        }

        auto const bytes = count * stride;

        // Reserve a contiguous, inaccessible address range. Pages are never
        // faulted in for this reservation; slots are overlaid with MAP_FIXED
        // file mappings afterwards.
        auto* const base = ::mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (base == MAP_FAILED)
        {
            MXL_WARN("Failed to reserve contiguous grain window of {} bytes; falling back to per-grain mappings.", bytes);
            return;
        }

        _base = static_cast<std::uint8_t*>(base);
        _bytes = bytes;
        _stride = stride;
        _count = count;
    }

    void ContiguousWindow::reset() noexcept
    {
        if (_base != nullptr)
        {
            // Unmap the whole reserved range in one call. This releases both the
            // remaining PROT_NONE reservation and any file mappings overlaid into
            // it, since those are created as externally-owned and do not unmap
            // themselves.
            (void)::munmap(_base, _bytes);
        }
        _base = nullptr;
        _bytes = 0;
        _stride = 0;
        _count = 0;
    }

    ContiguousWindow::~ContiguousWindow()
    {
        reset();
    }

    ContiguousWindow::ContiguousWindow(ContiguousWindow&& other) noexcept
        : _base{std::exchange(other._base, nullptr)}
        , _bytes{std::exchange(other._bytes, 0U)}
        , _stride{std::exchange(other._stride, 0U)}
        , _count{std::exchange(other._count, 0U)}
    {}

    ContiguousWindow& ContiguousWindow::operator=(ContiguousWindow&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            _base = std::exchange(other._base, nullptr);
            _bytes = std::exchange(other._bytes, 0U);
            _stride = std::exchange(other._stride, 0U);
            _count = std::exchange(other._count, 0U);
        }
        return *this;
    }
}
