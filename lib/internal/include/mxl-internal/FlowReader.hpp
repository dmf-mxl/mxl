// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <uuid.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include "FlowData.hpp"

namespace mxl::lib
{
    class FlowReader
    {
    public:
        ///
        /// Accessor for the flow id;
        /// \return The flow id
        ///
        uuids::uuid const& getId() const;

        ///
        /// Accessor for the flow domain;
        /// \return The flow domain
        ///
        std::filesystem::path const& getDomain() const;

        ///
        /// Accessor for the current mxlFlowInfo. A copy of the current structure is returned.
        /// The reader must be properly attached to the flow before invoking this method.
        /// \return A copy of the mxlFlowInfo
        ///
        virtual mxlFlowInfo getFlowInfo() = 0;

        ///
        /// Accessor for the underlying flow data.
        /// The reader must be properly attached to the flow before invoking this method.
        ///
        virtual FlowData& getFlowData() = 0;

        ///
        /// Dtor.
        ///
        virtual ~FlowReader();

    protected:
        explicit FlowReader(uuids::uuid&& flowId, std::filesystem::path const& domain);
        explicit FlowReader(uuids::uuid const& flowId, std::filesystem::path const& domain);

        ///
        /// A flow is considered valid if its flow data file exists, is accessible
        /// and its inode is the same as the one recorded in the flow info structure.
        /// \return true if the flow is valid, false otherwise.
        ///
        virtual bool isFlowValid() const = 0;

    private:
        uuids::uuid _flowId;
        std::filesystem::path _domain;
    };

} // namespace mxl::lib
