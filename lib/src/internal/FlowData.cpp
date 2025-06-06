#include "FlowData.hpp"

namespace mxl::lib
{
    FlowData::FlowData(char const* flowFilePath, AccessMode mode)
        : _flow{flowFilePath, mode, 0U}
    {}

    FlowData::~FlowData() = default;
}
