// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Exception.hpp
 * @brief Exception types for the fabrics OFI implementation
 *
 * This file defines exception classes and helper functions for error handling in the fabrics layer.
 *
 * ERROR HANDLING STRATEGY:
 * - Internally, the fabrics code uses C++ exceptions (Exception, FabricException)
 * - At the C API boundary, exceptions are caught and converted to mxlStatus codes
 * - This allows clean RAII and error propagation internally while maintaining C compatibility
 *
 * TWO EXCEPTION TYPES:
 * - **Exception**: Generic MXL fabrics exception carrying an mxlStatus code
 * - **FabricException**: Extends Exception to also carry a libfabric error code (fi_errno)
 *
 * HELPER FUNCTIONS:
 * - mxlStatusFromFiErrno(): Maps libfabric error codes to MXL status codes
 * - fiCall(): Template function that wraps libfabric API calls and throws on error
 *
 * USAGE PATTERN:
 * ```cpp
 * // Instead of:
 * int ret = fi_endpoint(domain, info, &ep, nullptr);
 * if (ret != 0) { /* handle error ... */ }
 *
 * // Use:
 * fiCall(fi_endpoint, "Failed to create endpoint", domain, info, &ep, nullptr);
 * // throws FabricException on error, otherwise continues
 * ```
 */

#pragma once

#include <exception>
#include <string>
#include <fmt/format.h>
#include <rdma/fi_errno.h>
#include <mxl/mxl.h>

namespace mxl::lib::fabrics::ofi
{
    /**
     * @brief Convert a libfabric error code to an mxlStatus code
     *
     * @param fiErrno Negative error code returned by a libfabric function (e.g., -FI_EAGAIN, -FI_ENOMEM)
     * @return mxlStatus Corresponding MXL status code
     *
     * Libfabric uses negative error codes defined in rdma/fi_errno.h. This function maps
     * common libfabric errors to their MXL equivalents.
     *
     * Common mappings:
     * - -FI_ENOMEM -> MXL_ERR_NO_MEMORY
     * - -FI_EINVAL -> MXL_ERR_INVALID_ARG
     * - -FI_ENOSYS -> MXL_ERR_NOT_SUPPORTED
     * - -FI_ETIMEDOUT -> MXL_ERR_TIMEOUT
     * - Everything else -> MXL_ERR_UNKNOWN
     */
    mxlStatus mxlStatusFromFiErrno(int fiErrno);

    /**
     * @class Exception
     * @brief Base exception class for the fabrics layer
     *
     * This exception carries an mxlStatus code that indicates the category of error.
     * It's used internally for error propagation and converted to status codes at the C API boundary.
     *
     * The class provides factory methods for creating specific error types:
     * - invalidArgument() for MXL_ERR_INVALID_ARG
     * - internal() for MXL_ERR_INTERNAL
     * - invalidState() for MXL_ERR_INVALID_STATE
     * - etc.
     *
     * These factory methods use fmt::format for type-safe, printf-style formatting.
     */
    class Exception : public std::exception
    {
    public:
        Exception(std::string msg, mxlStatus status);

        /** \brief Make any type of exception.
         */
        template<typename... T>
        static Exception make(mxlStatus status, fmt::format_string<T...> fmt, T&&... args)
        {
            return Exception(fmt::format(fmt, std::forward<T>(args)...), status);
        }

        /** \brief Make an MXL_ERR_INVALID_ARG exception.
         */
        template<typename... T>
        static Exception invalidArgument(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INVALID_ARG, fmt, std::forward<T>(args)...);
        }

        /** \brief Make an MXL_ERR_INTERNAL exception
         */
        template<typename... T>
        static Exception internal(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INTERNAL, fmt, std::forward<T>(args)...);
        }

        /** \brief Make an MXL_ERR_INVALID_STATE exception
         */
        template<typename... T>
        static Exception invalidState(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INVALID_STATE, fmt, std::forward<T>(args)...);
        }

        /** \brief Make an MXL_ERR_EXISTS exception
         */
        template<typename... T>
        static Exception exists(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_EXISTS, fmt, std::forward<T>(args)...);
        }

        /** \brief Make an MXL_ERR_NOT_FOUND exception
         */
        template<typename... T>
        static Exception notFound(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_NOT_FOUND, fmt, std::forward<T>(args)...);
        }

        /** \brief Make an MXL_ERR_INTERRUPTED exception
         */
        template<typename... T>
        static Exception interrupted(fmt::format_string<T...> fmt, T&&... args)
        {
            return make(MXL_ERR_INTERRUPTED, fmt, std::forward<T>(args)...);
        }

        /** \brief Return the mxlStatus status code that describes the condition
         * that led to the exception being thrown.
         */
        [[nodiscard]]
        mxlStatus status() const noexcept;

        /** \brief Implements std::exception, returns a descriptive string about the error.
         */
        [[nodiscard]]
        char const* what() const noexcept override;

    private:
        std::string _msg;
        mxlStatus _status;
    };

    /**
     * \brief An internal exception type that extends \see Exception to include an error
     * code returned from libfabric.
     */
    class FabricException : public Exception
    {
    public:
        FabricException(std::string msg, mxlStatus status, int fiErrno);

        /**
         * \brief Create a new exception object.
         *
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
     * \brief Call a libfabric function and check its return code.
     *
     * If the code is not FI_SUCCESS, throws an FabricException that includes the error code, and the message passed in as the
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
