// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/FlowIoFactory.hpp"

namespace mxl::lib
{
    struct MXL_EXPORT PosixFlowIoFactory : FlowIoFactory
    {
        /**
         * \param[in] watcher The domain watcher that the created flow writers will be registered to.
         */
        PosixFlowIoFactory(DomainWatcher::ptr watcher);

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

    private:
        DomainWatcher::ptr _watcher;
    };
}
