#include "Exception.hpp"
#include <rdma/fi_errno.h>

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

    FIException::FIException(std::string msg, mxlStatus status, int fiErrno)
        : Exception(std::move(msg), status)
        , _fiErrno(fiErrno)
    {}

    int FIException::fiErrno() const noexcept
    {
        return _fiErrno;
    }

    mxlStatus mxlStatusFromFiErrno(int fiErrno)
    {
        if (fiErrno == FI_EINTR)
        {
            return MXL_ERR_INTERRUPTED;
        }

        return MXL_ERR_UNKNOWN;
    }
}
