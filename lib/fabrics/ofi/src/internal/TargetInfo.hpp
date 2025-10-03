// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <rfl.hpp>
#include <uuid.h>
#include <fmt/format.h>
#include <rfl/Field.hpp>
#include <sys/types.h>
#include "mxl/fabrics.h"
#include "Address.hpp"
#include "Endpoint.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{

    // TargetInfo contains all the information required by an initiator to operate transfers to the given target.
    // In the context of libfabric this means, the fi_addr, all the buffer addresses and sizes, and the remote protection key.
    struct TargetInfo
    {
    public:
        TargetInfo() = default;

        TargetInfo(FabricAddress fabricAddress, std::vector<RemoteRegionGroup> regions, Endpoint::Id id = Endpoint::randomId())
            : fabricAddress(std::move(fabricAddress))
            , remoteRegionGroups(std::move(regions))
            , id(id)
        {}

        static TargetInfo* fromAPI(mxlTargetInfo api) noexcept;

        [[nodiscard]]
        ::mxlTargetInfo toAPI() noexcept;

        bool operator==(TargetInfo const& other) const noexcept;

    public:
        FabricAddress fabricAddress;
        std::vector<RemoteRegionGroup> remoteRegionGroups;
        Endpoint::Id id;
    };

    // namespace rlf_types
    //{
    struct TargetInfoRfl
    {
        rfl::Rename<"fabricAddress", FabricAddressRfl> fabricAddress;
        rfl::Rename<"regions", std::vector<RemoteRegionGroup>> regions;
        rfl::Rename<"identifier", std::string> identifier;

        static TargetInfoRfl from_class(TargetInfo const& ti)
        {
            rfl::Rename<"regions", std::vector<RemoteRegionGroup>> rflRegions;
            std::ranges::copy(ti.remoteRegionGroups.begin(), ti.remoteRegionGroups.end(), std::back_inserter(rflRegions.value()));
            return TargetInfoRfl{
                .fabricAddress = FabricAddressRfl::from_class(ti.fabricAddress), .regions = rflRegions, .identifier = fmt::to_string(ti.id)};
        }

        [[nodiscard]]
        TargetInfo to_class() const
        {
            return {fabricAddress.get().to_class(), regions.get(), std::stoul(identifier.get())};
        };
    };

    //}
}

namespace rfl::parsing
{
    namespace ofi = mxl::lib::fabrics::ofi;

    template<class ReaderType, class WriterType, class ProcessorsType>
    struct Parser<ReaderType, WriterType, ofi::TargetInfo, ProcessorsType>
        : public rfl::parsing::CustomParser<ReaderType, WriterType, ProcessorsType, ofi::TargetInfo, ofi::TargetInfoRfl>
    {};

}
