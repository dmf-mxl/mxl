// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl/mxl.h"
#include <exception>
#include <memory>
#include <string>
#include <mxl/version.h>
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/Instance.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PosixFlowIoFactory.hpp"

static char const g_mxl_version_string[] = MXL_VERSION_FULL;

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
        out_version->full = g_mxl_version_string;
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
        auto domainWatcher = std::make_shared<mxl::lib::DomainWatcher>(in_mxlDomain);
        auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>(domainWatcher);
        return reinterpret_cast<mxlInstance>(new mxl::lib::Instance{in_mxlDomain, opts, std::move(flowIoFactory), std::move(domainWatcher)});
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
