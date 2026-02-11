// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Exception.cpp
 * @brief Implementation of exception classes for the fabrics OFI layer
 */

#include "Exception.hpp"
#include <rdma/fi_errno.h>
#include "mxl/mxl.h"

namespace mxl::lib::fabrics::ofi
{
    // Construct generic Exception with message and MXL status code
    Exception::Exception(std::string msg, mxlStatus status)
        : _msg(std::move(msg))
        , _status(status)
    {}

    // Get the MXL status code associated with this exception
    mxlStatus Exception::status() const noexcept
    {
        return _status;
    }

    // Implement std::exception::what() - return error message
    char const* Exception::what() const noexcept
    {
        return _msg.c_str();
    }

    // Construct FabricException with message, MXL status, and libfabric error code
    FabricException::FabricException(std::string msg, mxlStatus status, int fiErrno)
        : Exception(std::move(msg), status)
        , _fiErrno(fiErrno)
    {}

    // Get the libfabric error code (negative value like -FI_EAGAIN, -FI_ENOMEM, etc.)
    int FabricException::fiErrno() const noexcept
    {
        return _fiErrno;
    }

    // Map libfabric error codes to MXL status codes
    mxlStatus mxlStatusFromFiErrno(int fiErrno)
    {
        // Note: libfabric error codes are negative (e.g., -FI_EAGAIN = -11)
        switch (fiErrno)
        {
            case -FI_EINTR:  return MXL_ERR_INTERRUPTED;  // Operation interrupted by signal
            case -FI_EAGAIN: return MXL_ERR_NOT_READY;    // Resource temporarily unavailable (would block)
            default:         return MXL_ERR_UNKNOWN;      // All other errors map to UNKNOWN
        }
        // TODO: Consider mapping more libfabric errors:
        // -FI_ENOMEM -> MXL_ERR_NO_MEMORY
        // -FI_EINVAL -> MXL_ERR_INVALID_ARG
        // -FI_ENOSYS -> MXL_ERR_NOT_SUPPORTED
        // -FI_ETIMEDOUT -> MXL_ERR_TIMEOUT
    }
}
