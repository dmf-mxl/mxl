// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowWriter.hpp"

namespace mxl::lib
{
    class DiscreteFlowWriter : public FlowWriter
    {
    public:
        virtual mxlStatus openGrain(std::uint64_t in_index, GrainInfo* out_grainInfo, std::uint8_t** out_payload) = 0;

        virtual mxlStatus commit(GrainInfo const& grainInfo) = 0;

        virtual mxlStatus cancel() = 0;

    protected:
        using FlowWriter::FlowWriter;
    };
}