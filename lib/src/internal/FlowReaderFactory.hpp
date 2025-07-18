// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include "ContinuousFlowData.hpp"
#include "ContinuousFlowReader.hpp"
#include "DiscreteFlowData.hpp"
#include "DiscreteFlowReader.hpp"

namespace mxl::lib
{
    class FlowManager;

    class FlowReaderFactory
    {
    public:
        virtual std::unique_ptr<DiscreteFlowReader> createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const = 0;
        virtual std::unique_ptr<ContinuousFlowReader> createContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const = 0;

    protected:
        ~FlowReaderFactory() = default;
    };
}
