// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowWriter.hpp"
#include <utility>

namespace mxl::lib
{
    FlowWriter::FlowWriter(uuids::uuid&& flowId, std::filesystem::path const& domain)
        : _flowId{std::move(flowId)}
        , _domain{domain}
    {}

    FlowWriter::FlowWriter(uuids::uuid const& flowId, std::filesystem::path const& domain)
        : _flowId{flowId}
        , _domain{domain}
    {}

    FlowWriter::FlowWriter::~FlowWriter() = default;

    uuids::uuid const& FlowWriter::getId() const
    {
        return _flowId;
    }

    bool FlowWriter::checkPermissions() const
    {
        // Verify that the domain exists, is a directory and that we can traverse and write into it.
        return std::filesystem::is_directory(_domain) && (::access(_domain.c_str(), X_OK | W_OK) == 0);
    }
}
