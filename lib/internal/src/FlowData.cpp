// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/FlowData.hpp"

namespace mxl::lib
{
    FlowData::FlowData(char const* flowFilePath, AccessMode mode, LockMode lockMode)
        : _flow{flowFilePath, mode, 0U, lockMode}
    {
        // see: https://en.cppreference.com/w/cpp/types/has_unique_object_representations.html
        static_assert(std::has_unique_object_representations_v<::mxlFlowInfo>,
            "mxlFlowInfo does not have a unique object representation, which means that its layout in memory can be different from how it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlFlowConfigInfo>,
            "mxlFlowConfigInfo does not have a unique object representation, which means that its layout in memory can be different from how it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlDiscreteFlowConfigInfo>,
            "mxlDiscreteFlowConfigInfo does not have a unique object representation, which means that its layout in memory can be different from how "
            "it is "
            "representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlContinuousFlowConfigInfo>,
            "mxlContinuousFlowConfigInfo does not have a unique object representation, which means that its layout in memory can be different from "
            "how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlFlowRuntimeInfo>,
            "mxlFlowRuntimeInfo does not have a unique object representation, which means that its layout in memory can be different from how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");
        static_assert(std::has_unique_object_representations_v<::mxlGrainInfo>,
            "mxlGrainInfo does not have a unique object representation, which means that its layout in memory can be different from how it "
            "is representetd in code. This was likely introduced by a change to the objects fields that introduced padding, or by adding non-trivial "
            "members to the object.");

        static_assert(sizeof(::mxlCommonFlowConfigInfo) == 128, "mxlCommonFlowConfigInfo does not have a size of 128 bytes");
        static_assert(sizeof(::mxlContinuousFlowConfigInfo) == 64, "mxlContinuousFlowConfigInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlDiscreteFlowConfigInfo) == 64, "mxlDiscreteFlowConfigInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlFlowConfigInfo) == 192, "mxlFlowConfigInfo does not have a size of 192 bytes");
        static_assert(sizeof(::mxlEventFlowConfigInfo) == 64, "mxlEventFlowConfigInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlFlowRuntimeInfo) == 64, "mxlFlowRuntimeInfo does not have a size of 64 bytes");
        static_assert(sizeof(::mxlFlowInfo) == 2048, "mxlFlowInfo does not have a size of 2048 bytes");
        static_assert(sizeof(::mxlGrainInfo) == 4096, "mxlGrainInfo does not have a size of 4096 bytes");
        static_assert(sizeof(::mxlEventInfo) == 512, "mxlEventInfo does not have a size of 512 bytes");
    }

    FlowData::~FlowData() = default;

    bool FlowData::isExclusive() const
    {
        return _flow.isExclusive();
    }

    bool FlowData::makeExclusive()
    {
        return _flow.makeExclusive();
    }
}
