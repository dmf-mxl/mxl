#pragma once

#include <exception>
#include <string>
#include <fmt/format.h>
#include <mxl/mxl.h>
#include <rdma/fi_errno.h>

namespace mxl::lib::fabrics::ofi
{
    mxlStatus mxlStatusFromFiErrno(int fiErrno);

    class Exception : public std::exception
    {
    public:
        Exception(std::string msg, mxlStatus status);

        template<typename... T>
        static Exception make(mxlStatus status, fmt::format_string<T...> fmt, T&&... args)
        {
            return Exception(fmt::format(fmt, std::forward<T>(args)...), status);
        }

        template<typename... T>
        static Exception invalidArgument(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INVALID_ARG, fmt, std::forward<T>(args)...);
        }

        template<typename... T>
        static Exception internal(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INTERNAL, fmt, std::forward<T>(args)...);
        }

        template<typename... T>
        static Exception invalidState(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INVALID_STATE, fmt, std::forward<T>(args)...);
        }

        template<typename... T>
        static Exception exists(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_EXISTS, fmt, std::forward<T>(args)...);
        }

        template<typename... T>
        static Exception notFound(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_NOT_FOUND, fmt, std::forward<T>(args)...);
        }

        template<typename... T>
        static Exception interrupted(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INTERRUPTED, fmt, std::forward<T>(args)...);
        }

        [[nodiscard]]
        mxlStatus status() const noexcept;

        [[nodiscard]]
        char const* what() const noexcept override;

    private:
        std::string _msg;
        mxlStatus _status;
    };

    class FIException : public Exception
    {
    public:
        FIException(std::string msg, mxlStatus status, int fiErrno);

        template<typename... T>
        static FIException make(int fiErrno, fmt::format_string<T...> fmt, T&&... args)
        {
            return FIException(fmt::format(fmt, std::forward<T>(args)...), mxlStatusFromFiErrno(fiErrno), fiErrno);
        }

        [[nodiscard]]
        int fiErrno() const noexcept;

    private:
        int _fiErrno;
    };

    template<typename F, typename... T>
    void fiCall(F fun, std::string_view msg, T... args)
    {
        auto result = fun(std::forward<T>(args)...);
        if (result != FI_SUCCESS)
        {
            auto str = ::fi_strerror(result);
            throw FIException::make(result, "{}: {}, code {}", msg, str, result);
        }
    }
}
