#include "Format.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <rdma/fabric.h>

namespace mxl::lib::fabrics::ofi
{
    std::string interfaceCapsString(std::uint64_t caps)
    {
        auto capStrings = std::vector<std::string>{};
        if (caps & MXL_FABRICS_IFACE_CAP_BLOCKING_OPERATIONS)
        {
            capStrings.emplace_back("BLOCKING_OPERATIONS");
        }
        if (caps & MXL_FABRICS_IFACE_CAP_REMOTE_WRITE)
        {
            capStrings.emplace_back("REMOTE_WRITE");
        }
        if (caps & MXL_FABRICS_IFACE_CAP_SEND_RECEIVE)
        {
            capStrings.emplace_back("SEND_RECEIVE");
        }
        if (capStrings.empty())
        {
            capStrings.emplace_back("<none>");
        }

        auto result = capStrings.front();
        for (auto it = capStrings.begin() + 1; it != capStrings.end(); ++it)
        {
            result.append("|");
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
