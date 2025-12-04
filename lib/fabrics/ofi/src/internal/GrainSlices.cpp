// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "GrainSlices.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{
    SliceRange SliceRange::make(std::uint16_t start, std::uint16_t end)
    {
        if (start > end)
        {
            throw Exception::invalidArgument("Invalid SliceRange: start must be less or equal to end");
        }

        return SliceRange{start, end};
    }

    std::uint32_t SliceRange::transferSize(std::uint32_t payloadOffset, std::uint32_t sliceSize) const noexcept
    {
        auto size = (_end - _start) * sliceSize;

        if (_start == 0)
        {
            size += payloadOffset;
        }

        return size;
    }

    std::uint32_t SliceRange::transferOffset(std::uint32_t payloadOffset, std::int32_t sliceSize) const noexcept
    {
        if (_start == 0)
        {
            return 0;
        }
        else
        {
            return payloadOffset + (_start * sliceSize);
        }
    }

    std::uint16_t SliceRange::start() const noexcept
    {
        return _start;
    }

    std::uint16_t SliceRange::end() const noexcept
    {
        return _end;
    }
}
