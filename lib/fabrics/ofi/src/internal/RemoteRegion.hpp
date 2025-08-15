#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <rfl.hpp>
#include <rdma/fi_rma.h>
#include <rfl/Rename.hpp>

namespace mxl::lib::fabrics::ofi
{
    class LocalRegionGroup;

    struct RemoteRegion
    {
        uint64_t addr;
        size_t len;
        uint64_t rkey;

        [[nodiscard]]
        ::fi_rma_iov toRmaIov() const noexcept;
    };

    class RemoteRegionGroup
    {
    public:
        RemoteRegionGroup(std::vector<RemoteRegion> group)
            : _inner(std::move(group))
            , _rmaIovs(rmaIovsFromGroup(_inner))
        {}

        [[nodiscard]]
        std::vector<RemoteRegion> const& view() const noexcept;

        [[nodiscard]]
        ::fi_rma_iov const* rmaIovs() const noexcept;

        [[nodiscard]]
        size_t count() const noexcept;

    private:
        static std::vector<::fi_rma_iov> rmaIovsFromGroup(std::vector<RemoteRegion> group) noexcept;

        std::vector<RemoteRegion> _inner;

        std::vector<::fi_rma_iov> _rmaIovs;
    };

    namespace rfl_types
    {
        struct RemoteRegionRfl
        {
            rfl::Rename<"addr", uint64_t> addr;
            rfl::Rename<"len", size_t> len;
            rfl::Rename<"rkey", uint64_t> rkey;

            static RemoteRegionRfl from_class(RemoteRegion const& reg)
            {
                return RemoteRegionRfl{.addr = reg.addr, .len = reg.len, .rkey = reg.rkey};
            }

            [[nodiscard]]
            RemoteRegion to_class() const
            {
                return {.addr = addr.get(), .len = len.get(), .rkey = rkey.get()};
            }
        };

        struct RemoteRegionGroupRfl
        {
            rfl::Rename<"group", std::vector<RemoteRegion>> group;

            static RemoteRegionGroupRfl from_class(RemoteRegionGroup const& group)
            {
                return RemoteRegionGroupRfl{.group = group.view()};
            };

            [[nodiscard]]
            RemoteRegionGroup to_class() const
            {
                return group.get();
            };
        };

    };
}

namespace rfl::parsing
{
    namespace ofi = mxl::lib::fabrics::ofi;

    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, ofi::RemoteRegion, ProcessorsType>
        : public rfl::parsing::CustomParser<ReaderType, WriterType, ProcessorsType, ofi::RemoteRegion, ofi::rfl_types::RemoteRegionRfl>
    {};

    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, ofi::RemoteRegionGroup, ProcessorsType>
        : public rfl::parsing::CustomParser<ReaderType, WriterType, ProcessorsType, ofi::RemoteRegionGroup, ofi::rfl_types::RemoteRegionGroupRfl>
    {};
}
