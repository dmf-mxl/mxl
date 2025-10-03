// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FILogging.hpp"
#include <atomic>
#include <rdma/fabric.h>
#include <rdma/fi_ext.h>
#include <rdma/providers/fi_log.h>
#include <spdlog/common.h>
#include "mxl/fabrics.h"
#include "spdlog/spdlog.h"
#include "Exception.hpp"
#include "FIVersion.hpp"

namespace mxl::lib::fabrics::ofi
{
    int fiLogEnabled(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, std::uint64_t flags);
    int fiLogReady(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, std::uint64_t flags, std::uint64_t* showtime);
    void fiLog(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, char const*, int line, char const* msgIn);

    class FILogging
    {
    public:
        FILogging() = default;

        void init()
        {
            // It is safe to call a logging functions on the libfabric side
            // while we initialize logging here. We just need to make sure
            // to initialize logging only once.
            if (_isInit.exchange(true, std::memory_order_relaxed))
            {
                return;
            }

            auto ops = ::fi_ops_log{
                .size = sizeof(::fi_ops_log),
                .enabled = &fiLogEnabled,
                .ready = &fiLogReady,
                .log = &fiLog,
            };

            auto logging = ::fid_logging{
                .fid = ::fid{},
                .ops = &ops,
            };

            fiCall(::fi_import_log, "Failed to initialize logging", fiVersion(), 0, &logging);
        }

    private:
        friend int fiLogEnabled(const struct fi_provider*, enum fi_log_level, enum fi_log_subsys, std::uint64_t);

        [[nodiscard]]
        fi_log_level level() const noexcept
        {
            return FI_LOG_WARN;
        }

        [[nodiscard]]
        bool isProviderLoggingEnabled(const struct fi_provider*) const noexcept
        {
            return true;
        }

        [[nodiscard]]
        bool isSubsystemLoggingEnabled(enum fi_log_subsys) const noexcept
        {
            return true;
        }

        std::atomic_bool _isInit;
    };

    static FILogging logging;

    char const* fiLogSubsystemName(enum fi_log_subsys subsys)
    {
        switch (subsys)
        {
            case FI_LOG_CORE:    return "core";
            case FI_LOG_FABRIC:  return "fabric";
            case FI_LOG_DOMAIN:  return "domain";
            case FI_LOG_EP_CTRL: return "ep_ctrl";
            case FI_LOG_EP_DATA: return "ep_data";
            case FI_LOG_AV:      return "av";
            case FI_LOG_CQ:      return "cq";
            case FI_LOG_EQ:      return "eq";
            case FI_LOG_MR:      return "mr";
            case FI_LOG_CNTR:    return "cntr";
            default:             return "";
        }
    }

    spdlog::level::level_enum fiLevelToSpdlogLevel(enum fi_log_level level)
    {
        switch (level)
        {
            case FI_LOG_WARN:  return spdlog::level::warn;
            case FI_LOG_TRACE: return spdlog::level::trace;
            case FI_LOG_INFO:  return spdlog::level::info;
            case FI_LOG_DEBUG: return spdlog::level::debug;
        }
    }

    int fiLogEnabled(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, std::uint64_t flags)
    {
        MXL_FABRICS_UNUSED(flags);

        return level <= logging.level() && logging.isProviderLoggingEnabled(prov) && logging.isSubsystemLoggingEnabled(subsys);
    }

    int fiLogReady(const struct fi_provider*, enum fi_log_level, enum fi_log_subsys, std::uint64_t, std::uint64_t*)
    {
        return 0;
    }

    void fiLog(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, char const*, int line, char const* msgIn)
    {
        std::string msg{msgIn};

        if (!msg.empty() && msg[msg.size() - 1] == '\n')
        {
            msg.erase(msg.size() - 1);
        }

        spdlog::log(
            {fmt::format("libfabric:{}:{}", fiLogSubsystemName(subsys), prov->name).c_str(), line, ""}, fiLevelToSpdlogLevel(level), "{}", msg);
    }

    // Initialize logging
    void fiInitLogging()
    {
        logging.init();
    }
}
