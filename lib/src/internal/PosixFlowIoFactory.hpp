// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FlowIoFactory.hpp"

namespace mxl::lib
{
    struct PosixFlowIoFactory : FlowIoFactory
    {
        /** \see FlowReaderFactory::createDiscreteFlowReader() */
        virtual std::unique_ptr<DiscreteFlowReader> createDiscreteFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const override;
        /** \see FlowReaderFactory::createContinuousFlowReader() */
        virtual std::unique_ptr<ContinuousFlowReader> createContinuousFlowReader(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const override;
        /** \see FlowWriterFactory::createDiscreteFlowWriter() */
        virtual std::unique_ptr<DiscreteFlowWriter> createDiscreteFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<DiscreteFlowData>&& data) const override;
        /** \see FlowWriterFactory::createContinuousFlowWriter() */
        virtual std::unique_ptr<ContinuousFlowWriter> createContinuousFlowWriter(FlowManager const& manager, uuids::uuid const& flowId,
            std::unique_ptr<ContinuousFlowData>&& data) const override;

        ~PosixFlowIoFactory();
    };
}
