// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "mxl-internal/FlowData.hpp"

namespace mxl::lib
{
    class MXL_EXPORT FlowReader
    {
    public:
        /**
         * Accessor for the flow id;
         * \return The flow id
         */
        [[nodiscard]]
        uuids::uuid const& getId() const;

        /**
         * Accessor for the flow domain;
         * \return The flow domain
         */
        [[nodiscard]]
        std::filesystem::path const& getDomain() const;

        /**
         * Accessor for the underlying flow data.
         * The flow reader must first open the flow before invoking this method.
         */
        [[nodiscard]]
        virtual FlowData const& getFlowData() const = 0;

        /**
         * Accessor for the current mxlFlowInfo. A copy of the current structure is returned.
         * The reader must be properly attached to the flow before invoking this method.
         * \return A copy of the mxlFlowInfo
         */
        [[nodiscard]]
        virtual mxlFlowInfo getFlowInfo() const = 0;

        /**
         * Accessor for the immutable metadata that describes the currently opened flow.
         * The reader must be properly attached to the flow before invoking this method.
         * \return A copy of the mxlFlowConfigInfo of the currently opened flow.
         */
        [[nodiscard]]
        virtual mxlFlowConfigInfo getFlowConfigInfo() const = 0;

        /**
         * Accessor for the current metadata that describes the current runtime state of
         * the currently opened flow. The reader must be properly attached to the flow before
         * invoking this method.
         * \return A copy of the current mxlFlowRuntimeInfo of the currently opened flow.
         */
        [[nodiscard]]
        virtual mxlFlowRuntimeInfo getFlowRuntimeInfo() const = 0;

        /** Destructor. */
        virtual ~FlowReader();

    protected:
        /**
         * A flow is considered valid if its flow data file exists, is accessible
         * and its inode is the same as the one recorded in the flow info structure.
         * \return true if the flow is valid, false otherwise.
         */
        [[nodiscard]]
        virtual bool isFlowValid() const = 0;

    protected:
        explicit FlowReader(uuids::uuid&& flowId, std::filesystem::path const& domain);
        explicit FlowReader(uuids::uuid const& flowId, std::filesystem::path const& domain);

    private:
        uuids::uuid _flowId;
        std::filesystem::path _domain;
    };

} // namespace mxl::lib
