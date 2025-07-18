// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#include "mxl/mxl.h"
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <mxl/version.h>
#include "internal/Instance.hpp"
#include "internal/Logging.hpp"
#include "internal/PosixFlowIoFactory.hpp"

extern "C"
MXL_EXPORT
mxlStatus mxlGetVersion(mxlVersionType* out_version)
{
    if (out_version != nullptr)
    {
        out_version->major = MXL_VERSION_MAJOR;
        out_version->minor = MXL_VERSION_MINOR;
        out_version->bugfix = MXL_VERSION_PATCH;
        out_version->build = MXL_VERSION_BUILD;
        return MXL_STATUS_OK;
    }
    else
    {
        return MXL_ERR_INVALID_ARG;
    }
}

extern "C" MXL_EXPORT
mxlInstance mxlCreateInstance(char const* in_mxlDomain, char const* in_options)
{
    try
    {
        auto const opts = (in_options != nullptr) ? in_options : "";
        auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>();
        return reinterpret_cast<mxlInstance>(new mxl::lib::Instance{in_mxlDomain, opts, std::move(flowIoFactory)});
    }
    catch (std::exception& e)
    {
        MXL_ERROR("Failed to create instance : {}", e.what());
        return nullptr;
    }
    catch (...)
    {
        MXL_ERROR("Failed to create instance");
        return nullptr;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlDestroyInstance(mxlInstance in_instance)
{
    try
    {
        delete mxl::lib::to_Instance(in_instance);
        return (in_instance != nullptr) ? MXL_STATUS_OK : MXL_ERR_INVALID_ARG;
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}

extern "C"
MXL_EXPORT
mxlStatus mxlGarbageCollectFlows(mxlInstance in_instance)
{
    try
    {
        if (auto const instance = mxl::lib::to_Instance(in_instance); instance != nullptr)
        {
            MXL_DEBUG("Starting garbage collection of flows");
            [[maybe_unused]]
            auto count = instance->garbageCollect();
            MXL_DEBUG("Garbage collected {} flows", count);
            return MXL_STATUS_OK;
        }
        else
        {
            return MXL_ERR_INVALID_ARG;
        }
    }
    catch (...)
    {
        return MXL_ERR_UNKNOWN;
    }
}