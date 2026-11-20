// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Shared SDK status names for command-line tools.
 */

#pragma once

#include <mxl/mxl.h>

namespace mxl::tools
{
    /// Return an SDK status name for an error message.
    /// @param status Status returned by an SDK call.
    /// @return A static status name, or MXL_ERR_UNRECOGNIZED for an unknown value.
    [[nodiscard]]
    constexpr char const* statusToString(::mxlStatus status) noexcept
    {
        switch (status)
        {
            case MXL_STATUS_OK:                  return "MXL_STATUS_OK";
            case MXL_ERR_UNKNOWN:                return "MXL_ERR_UNKNOWN";
            case MXL_ERR_FLOW_NOT_FOUND:         return "MXL_ERR_FLOW_NOT_FOUND";
            case MXL_ERR_OUT_OF_RANGE_TOO_LATE:  return "MXL_ERR_OUT_OF_RANGE_TOO_LATE";
            case MXL_ERR_OUT_OF_RANGE_TOO_EARLY: return "MXL_ERR_OUT_OF_RANGE_TOO_EARLY";
            case MXL_ERR_INVALID_FLOW_READER:    return "MXL_ERR_INVALID_FLOW_READER";
            case MXL_ERR_INVALID_FLOW_WRITER:    return "MXL_ERR_INVALID_FLOW_WRITER";
            case MXL_ERR_TIMEOUT:                return "MXL_ERR_TIMEOUT";
            case MXL_ERR_INVALID_ARG:            return "MXL_ERR_INVALID_ARG";
            case MXL_ERR_CONFLICT:               return "MXL_ERR_CONFLICT";
            case MXL_ERR_PERMISSION_DENIED:      return "MXL_ERR_PERMISSION_DENIED";
            case MXL_ERR_FLOW_INVALID:           return "MXL_ERR_FLOW_INVALID";
            case MXL_ERR_STRLEN:                 return "MXL_ERR_STRLEN";
            case MXL_ERR_INTERRUPTED:            return "MXL_ERR_INTERRUPTED";
            case MXL_ERR_NO_FABRIC:              return "MXL_ERR_NO_FABRIC";
            case MXL_ERR_INVALID_STATE:          return "MXL_ERR_INVALID_STATE";
            case MXL_ERR_INTERNAL:               return "MXL_ERR_INTERNAL";
            case MXL_ERR_NOT_READY:              return "MXL_ERR_NOT_READY";
            case MXL_ERR_NOT_FOUND:              return "MXL_ERR_NOT_FOUND";
            case MXL_ERR_EXISTS:                 return "MXL_ERR_EXISTS";
            case MXL_ERR_UNSUPPORTED_OPERATION:  return "MXL_ERR_UNSUPPORTED_OPERATION";
            default:                             return "MXL_ERR_UNRECOGNIZED";
        }
    }
}
