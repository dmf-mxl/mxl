// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0
#include "Format.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <rdma/fabric.h>

namespace mxl::lib::fabrics::ofi
{
    std::string interfaceCapsString(std::uint64_t caps)
    {
        auto resultLength = std::size_t{0};
        auto capStrings = std::vector<std::string>{};
        if ((caps & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS) != 0)
        {
            resultLength += capStrings.emplace_back("BLOCKING_OPERATIONS").size();
        }
        if ((caps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE) != 0)
        {
            resultLength += capStrings.emplace_back("REMOTE_WRITE").size();
        }
        if ((caps & MXL_FABRICS_IFACE_CAP_SEND_RECEIVE) != 0)
        {
            resultLength += capStrings.emplace_back("SEND_RECEIVE").size();
        }
        if (capStrings.empty())
        {
            resultLength += capStrings.emplace_back("<none>").size();
        }

        resultLength += (capStrings.size() - 1); // separating '|' chars

        auto result = capStrings.front();
        result.reserve(resultLength + (capStrings.size() - 1));
        for (auto it = capStrings.begin() + 1; it != capStrings.end(); ++it)
        {
            result.push_back('|');
            result.append(*it);
        }

        return result;
    }

    std::string fiToString(void* any, ::fi_type type)
    {
        auto buf = std::string((type == FI_TYPE_INFO) ? 4096 : 1024, '\0');
        ::fi_tostr_r(buf.data(), buf.size(), any, type);
        buf.resize(::strlen(buf.data()));
        return buf;
    }
}
