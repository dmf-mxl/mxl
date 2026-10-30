// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowReader.hpp"
#include <fstream>
#include <utility>
#ifndef _WIN32
#   include <unistd.h>
#endif
#include "mxl-internal/PathUtils.hpp"

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

    bool FlowReader::checkPermissions() const
    {
        auto const flowDefPath = makeFlowDescriptorFilePath(_domain, uuids::to_string(_flowId));
#ifdef _WIN32
        // _waccess() only checks the read-only attribute, not the access control list. Opening the file checks both.
        return std::filesystem::is_regular_file(flowDefPath) && std::ifstream{flowDefPath, std::ios::binary}.is_open();
#else
        return std::filesystem::is_regular_file(flowDefPath) && (::access(flowDefPath.c_str(), R_OK) == 0);
#endif
    }
}
