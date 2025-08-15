#include "FIVersion.hpp"
#include <rdma/fabric.h>

namespace mxl::lib::fabrics::ofi
{
    ::uint32_t fiVersion() noexcept
    {
        return FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION);
    }
}
