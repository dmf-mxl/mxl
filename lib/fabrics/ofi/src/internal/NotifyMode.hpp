// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string_view>

namespace mxl::lib::fabrics::ofi
{
    /** \brief How an initiator tells a target which ring slot (and slice range) an RMA write filled.
     *
     * Send exists for providers that can do RMA writes but cannot deliver remote CQ data.
     */
    enum class NotifyMode
    {
        Immediate, ///< The last write of a transfer carries the value as remote CQ data (FI_REMOTE_CQ_DATA).
        Send,      ///< The writes carry no CQ data. A 4 byte message with the same value follows them on the same endpoint.
    };

    /** \brief Name of the environment variable that forces a notify mode. Accepted values are "imm" and "send".
     */
    constexpr auto notifyModeEnvVar = std::string_view{"MXL_FABRICS_NOTIFY"};

    /** \brief Convert a notify mode to the string used in serialized TargetInfo and in the environment variable.
     * \param[in] mode The notify mode to convert.
     * \return "imm" or "send".
     */
    [[nodiscard]]
    constexpr std::string_view notifyModeToString(NotifyMode mode) noexcept
    {
        switch (mode)
        {
            case NotifyMode::Send: return "send";
            default:               return "imm";
        }
    }

    /** \brief Read the notify mode forced through the MXL_FABRICS_NOTIFY environment variable.
     * \return The forced notify mode, or std::nullopt if the variable is not set.
     * \throws Exception if the variable holds a value other than "imm" or "send".
     */
    [[nodiscard]]
    std::optional<NotifyMode> notifyModeFromEnvironment();

    /** \brief Parse a notify mode string.
     * \param[in] s The string to parse.
     * \return The notify mode, or std::nullopt if the string is neither "imm" nor "send".
     */
    [[nodiscard]]
    constexpr std::optional<NotifyMode> notifyModeFromString(std::string_view s) noexcept
    {
        if (s == "imm")
        {
            return NotifyMode::Immediate;
        }
        if (s == "send")
        {
            return NotifyMode::Send;
        }
        return std::nullopt;
    }
}
