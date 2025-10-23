// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <exception>
#include <string>
#include <fmt/format.h>
#include <rdma/fi_errno.h>
#include <mxl/mxl.h>

namespace mxl::lib::fabrics::ofi
{
    /**
     * Convert an error code returned by libfabric to an appropriate mxlStatus code.
     */
    mxlStatus mxlStatusFromFiErrno(int fiErrno);

    /**
     * Custom exception type used in the fabrics library. Used to
     * propagate information about error conditions to the api surface by
     * carrying a status code.
     */
    class Exception : public std::exception
    {
    public:
        Exception(std::string msg, mxlStatus status);

        /// Make any type of exception
        template<typename... T>
        static Exception make(mxlStatus status, fmt::format_string<T...> fmt, T&&... args)
        {
            return Exception(fmt::format(fmt, std::forward<T>(args)...), status);
        }

        /// Make an MXL_ERR_INVALID_ARG exception
        template<typename... T>
        static Exception invalidArgument(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INVALID_ARG, fmt, std::forward<T>(args)...);
        }

        /// Make an MXL_ERR_INTERNAL exception
        template<typename... T>
        static Exception internal(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INTERNAL, fmt, std::forward<T>(args)...);
        }

        /// Make an MXL_ERR_INVALID_STATE exception
        template<typename... T>
        static Exception invalidState(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INVALID_STATE, fmt, std::forward<T>(args)...);
        }

        /// Make an MXL_ERR_EXISTS exception
        template<typename... T>
        static Exception exists(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_EXISTS, fmt, std::forward<T>(args)...);
        }

        /// Make an MXL_ERR_NOT_FOUND exception
        template<typename... T>
        static Exception notFound(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_NOT_FOUND, fmt, std::forward<T>(args)...);
        }

        /// Make an MXL_ERR_INTERRUPTED exception
        template<typename... T>
        static Exception interrupted(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INTERRUPTED, fmt, std::forward<T>(args)...);
        }

        /// Return the mxlStatus status code that describes the condition
        /// that led to the exception being thrown.
        [[nodiscard]]
        mxlStatus status() const noexcept;

        /// Implements std::exception, returns a descriptive string about the error.
        [[nodiscard]]
        char const* what() const noexcept override;

    private:
        std::string _msg;
        mxlStatus _status;
    };

    /**
     * An internal exception type that extends \see Exception to include an error
     * code returned from libfabric.
     */
    class FabricException : public Exception
    {
    public:
        FabricException(std::string msg, mxlStatus status, int fiErrno);

        /**
         * Create a new exception object.
         * \param fiErrno The error code returned by the libfabric function.
         * \param fmt Format string
         * \param ...args Format args
         */
        template<typename... T>
        static FabricException make(int fiErrno, fmt::format_string<T...> fmt, T&&... args)
        {
            return FabricException(fmt::format(fmt, std::forward<T>(args)...), mxlStatusFromFiErrno(fiErrno), fiErrno);
        }

        [[nodiscard]]
        int fiErrno() const noexcept;

    private:
        int _fiErrno;
    };

    /**
     * Call a libfabric function and check its return code. If the code is not FI_SUCCESS,
     * throws an FabricException that includes the error code, and the message passed in as the
     * second argument.
     */
    template<typename F, typename... T>
    int fiCall(F fun, std::string_view msg, T... args)
    {
        int result = fun(std::forward<T>(args)...);
        if (result < 0)
        {
            auto str = ::fi_strerror(result);
            throw FabricException::make(result, "{}: {}, code {}", msg, str, result);
        }

        return result;
    }
}
