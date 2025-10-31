// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "Exception.hpp"
#include <rdma/fi_errno.h>
#include "mxl/mxl.h"

namespace mxl::lib::fabrics::ofi
{
    Exception::Exception(std::string msg, mxlStatus status)
        : _msg(std::move(msg))
        , _status(status)
    {}

    mxlStatus Exception::status() const noexcept
    {
        return _status;
    }

    char const* Exception::what() const noexcept
    {
        return _msg.c_str();
    }

    FabricException::FabricException(std::string msg, mxlStatus status, int fiErrno)
        : Exception(std::move(msg), status)
        , _fiErrno(fiErrno)
    {}

    int FabricException::fiErrno() const noexcept
    {
        return _fiErrno;
    }

    mxlStatus mxlStatusFromFiErrno(int fiErrno)
    {
        switch (fiErrno)
        {
            case -FI_EINTR:  return MXL_ERR_INTERRUPTED;
            case -FI_EAGAIN: return MXL_ERR_NOT_READY;
            default:         return MXL_ERR_UNKNOWN;
        }
    }
}
