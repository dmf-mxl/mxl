// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "FlowReader.hpp"
#include <utility>

namespace mxl::lib
{
    FlowReader::FlowReader(uuids::uuid&& flowId, std::filesystem::path const& domain)
        : _flowId{std::move(flowId)}
        , _domain{domain}
    {}

    FlowReader::FlowReader(uuids::uuid const& flowId, std::filesystem::path const& domain)
        : _flowId{flowId}
        , _domain{domain}
    {}

    FlowReader::FlowReader::~FlowReader() = default;

    uuids::uuid const& FlowReader::getId() const
    {
        return _flowId;
    }

    std::filesystem::path const& FlowReader::getDomain() const
    {
        return _domain;
    }
}
