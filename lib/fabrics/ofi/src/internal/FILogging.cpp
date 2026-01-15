// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "FILogging.hpp"
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <string_view>
#include <rdma/fabric.h>
#include <rdma/fi_ext.h>
#include <rdma/providers/fi_log.h>
#include <spdlog/common.h>
#include "spdlog/spdlog.h"
#include "Exception.hpp"
#include "FabricVersion.hpp"

namespace mxl::lib::fabrics::ofi
{
    namespace
    {
        constexpr static auto fiLogLevelStrings = std::array<std::pair<std::string_view, ::fi_log_level>, 4>{
            {{"trace", FI_LOG_TRACE}, {"debug", FI_LOG_DEBUG}, {"info", FI_LOG_INFO}, {"warn", FI_LOG_WARN}}
        };
    }

    int fiLogEnabled(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, std::uint64_t flags);
    int fiLogReady(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, std::uint64_t flags, std::uint64_t* showtime);
    void fiLog(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, char const*, int line, char const* msgIn);

    class FILogging
    {
    public:
        void init()
        {
            // It is safe to call a logging functions on the libfabric side
            // while we initialize logging here. We just need to make sure
            // to initialize logging only once.
            if (_isInit.exchange(true, std::memory_order_relaxed))
            {
                return;
            }

            auto fiLogLevelCStr = ::getenv("FI_LOG_LEVEL");
            if (fiLogLevelCStr != nullptr)
            {
                auto fiLogLevelStr = std::string{fiLogLevelCStr};
                std::ranges::transform(fiLogLevelStr, fiLogLevelStr.begin(), [](char const c) -> char { return std::tolower(c); });
                auto it = std::ranges::find_if(fiLogLevelStrings,
                    [fiLogLevelStr](std::pair<std::string_view, ::fi_log_level> const& item) { return fiLogLevelStr == item.first; });
                if (it != fiLogLevelStrings.end())
                {
                    _level = it->second;
                }
                else
                {
                    _level = FI_LOG_WARN;
                }
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
            return _level;
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
        ::fi_log_level _level;
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
            default:           return spdlog::level::warn;
        }
    }

    int fiLogEnabled(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, std::uint64_t)
    {
        return (level <= logging.level() || logging.level() == FI_LOG_TRACE) && logging.isProviderLoggingEnabled(prov) &&
               logging.isSubsystemLoggingEnabled(subsys);
    }

    int fiLogReady(const struct fi_provider*, enum fi_log_level, enum fi_log_subsys, std::uint64_t, std::uint64_t*)
    {
        return 0;
    }

    void fiLog(const struct fi_provider* prov, enum fi_log_level level, enum fi_log_subsys subsys, char const*, int line, char const* msgIn)
    {
        auto msg = std::string{msgIn};
        if (msg.ends_with('\n'))
        {
            msg.pop_back();
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
