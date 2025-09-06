// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <mxl/fabrics.h>
#include <mxl/mxl.h>
#include "TargetInfo.hpp"

namespace mxl::lib::fabrics::ofi
{

    class Target
    {
    protected:
        struct ImmediateDataLocation
        {
            uint64_t data;

            LocalRegion toLocalRegion() noexcept;
        };

    public:
        struct ReadResult
        {
            std::optional<uint64_t> grainAvailable{std::nullopt};
        };

        virtual ~Target() = default;

        virtual ReadResult read() = 0;
        virtual ReadResult readBlocking(std::chrono::steady_clock::duration timeout) = 0;
    };

    class TargetWrapper
    {
    public:
        TargetWrapper() = default;
        ~TargetWrapper() = default;

        static TargetWrapper* fromAPI(mxlFabricsTarget api) noexcept;
        mxlFabricsTarget toAPI() noexcept;

        Target::ReadResult read();
        Target::ReadResult readBlocking(std::chrono::steady_clock::duration timeout);

        std::unique_ptr<TargetInfo> setup(mxlTargetConfig const& config);

    private:
        std::unique_ptr<Target> _inner;
    };
}
