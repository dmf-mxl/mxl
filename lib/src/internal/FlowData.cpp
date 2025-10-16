// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "FlowData.hpp"

namespace mxl::lib
{
    FlowData::FlowData(char const* flowFilePath, AccessMode mode)
        : _flow{flowFilePath, mode, 0U}
    {
        // see: https://en.cppreference.com/w/cpp/types/has_unique_object_representations.html
        static_assert(std::has_unique_object_representations_v<::mxlFlowInfo>,
            "mxlFlowInfo does not have a unique object representation, which means that its layout in memory can be different from how it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlDiscreteFlowInfo>,
            "mxlDiscreteFlowInfo does not have a unique object representation, which means that its layout in memory can be different from how it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlContinuousFlowInfo>,
            "mxlContinuousFlowInfo does not have a unique object representation, which means that its layout in memory can be different from how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlGrainInfo>,
            "mxlGrainInfo does not have a unique object representation, which means that its layout in memory can be different from how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");

        static_assert(sizeof(::mxlCommonFlowInfo) == 128, "mxlCommonFlowInfo does not have a size of 128 bytes");
        static_assert(sizeof(::mxlContinuousFlowInfo) == 128, "mxlContinuousFlowInfo does not have a size of 128 bytes");
        static_assert(sizeof(::mxlDiscreteFlowInfo) == 128, "mxlDiscreteFlowInfo does not have a size of 128 bytes");
        static_assert(sizeof(::mxlFlowInfo) == 4096, "mxlFlowInfo does not have a size of 4096 bytes");
        static_assert(sizeof(::mxlGrainInfo) == 4096, "mxlGrainInfo does not have a size of 4096 bytes");
    }

    FlowData::~FlowData() = default;
}
