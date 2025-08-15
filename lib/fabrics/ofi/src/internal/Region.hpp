#pragma once

#include <cstdint>
#include <vector>
#include <uuid.h>
#include <bits/types/struct_iovec.h>
#include "internal/FlowData.hpp"
#include "mxl/fabrics.h"

namespace mxl::lib::fabrics::ofi
{
    class Region
    {
    public:
        explicit Region(std::uintptr_t base, size_t size)
            : base(base)
            , size(size)
            , _iovec(iovecFromRegion(base, size))
        {}

        virtual ~Region() = default;

        std::uintptr_t base;
        size_t size;

        [[nodiscard]]
        ::iovec const* as_iovec() const noexcept;

        [[nodiscard]]
        ::iovec to_iovec() const noexcept;

    private:
        static ::iovec iovecFromRegion(std::uintptr_t, size_t) noexcept;

        ::iovec _iovec;
    };

    class RegionGroup

    {
    public:
        explicit RegionGroup() = default;

        explicit RegionGroup(std::vector<Region> inner)
            : _inner(std::move(inner))
            , _iovecs(iovecsFromGroup(_inner))
        {}

        [[nodiscard]]
        std::vector<Region> const& view() const noexcept;

        [[nodiscard]]
        ::iovec const* as_iovec() const noexcept;

    private:
        static std::vector<::iovec> iovecsFromGroup(std::vector<Region> const& group) noexcept;

        std::vector<Region> _inner;

        std::vector<::iovec> _iovecs;
    };

    class RegionGroups
    {
    public:
        static RegionGroups fromFlow(FlowData& flow);

        static RegionGroups* fromAPI(mxlRegions) noexcept;
        [[nodiscard]]
        mxlRegions toAPI() noexcept;

        [[nodiscard]]
        std::vector<RegionGroup> const& view() const noexcept;

    private:
        explicit RegionGroups(std::vector<RegionGroup> inner)
            : _inner(std::move(inner))
        {}

        std::vector<RegionGroup> _inner;
    };

}
