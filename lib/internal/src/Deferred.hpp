// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Deferred.hpp
 * @brief Implements RAII-style deferred execution using scope guards
 *
 * This file provides a defer() utility similar to Go's defer statement or C++'s scope_guard idiom.
 * Functions passed to defer() are automatically executed when the returned Deferred object goes
 * out of scope, ensuring cleanup code runs even in exception paths.
 *
 * Key design decisions:
 * - The Deferred class is move-only to prevent accidental copies that could cause double-execution
 * - The [[nodiscard]] attribute on defer() forces users to bind the result, preventing immediate execution
 * - constexpr support allows compile-time usage where applicable
 * - noexcept specification is conditional based on the callable's exception guarantee
 *
 * Common usage pattern:
 *   auto cleanup = defer([&]() { resource.release(); });
 *   // ... work with resource ...
 *   // cleanup automatically runs when scope exits
 */

#pragma once

#include <utility>

namespace mxl::lib
{
    /**
     * @brief RAII wrapper that executes a callable when it goes out of scope
     *
     * This is an internal helper class created by the defer() function. It provides
     * scope-based execution guarantees for cleanup operations. The class is move-only
     * to prevent accidental copies that could result in multiple executions.
     *
     * @tparam F The type of the callable to execute on destruction
     *
     * @note This class should never be instantiated directly - use defer() instead
     * @see defer()
     */
    template<typename F>
    class Deferred
    {
    public:
        // Delete all copy and move operations to ensure single execution on scope exit
        Deferred(Deferred<F> const&) = delete;
        Deferred(Deferred<F>&&) = delete;
        Deferred& operator=(Deferred<F> const&) = delete;
        Deferred& operator=(Deferred<F>&&) = delete;

        /**
         * @brief Destructor that executes the deferred callable
         *
         * This is where the deferred function actually runs. The destructor is marked
         * noexcept conditionally - it's noexcept only if the callable itself is noexcept.
         * This allows proper exception propagation while maintaining strong exception safety.
         *
         * @note The callable is invoked exactly once during destruction
         */
        constexpr ~Deferred() noexcept(std::is_nothrow_invocable_v<F>)
        {
            _f();
        }

    private:
        /**
         * @brief Private constructor - only defer() can create instances
         *
         * The constructor is private to enforce that Deferred objects are only
         * created through the defer() function, which provides the proper interface
         * and documentation for users.
         *
         * @param f The callable to execute on destruction (forwarding reference)
         */
        constexpr Deferred(F&& f)
            : _f(std::forward<F>(f))
        {}

        /**
         * @brief Grant defer() access to the private constructor
         *
         * This friend declaration allows defer() to construct Deferred objects
         * while keeping the constructor private from other code.
         */
        template<typename N>
        friend constexpr Deferred<N> defer(N&& f) noexcept;

    private:
        F _f; ///< The stored callable to execute on destruction
    };

    /**
     * @brief Create a scope guard that executes a callable on scope exit
     *
     * This function provides Go-style "defer" semantics for C++. The returned Deferred
     * object will execute the provided callable when it goes out of scope, regardless
     * of how the scope exits (normal return, exception, etc.).
     *
     * The [[nodiscard]] attribute ensures users don't accidentally write:
     *   defer(cleanup);  // ERROR: executes immediately!
     * Instead forcing:
     *   auto d = defer(cleanup);  // OK: executes on scope exit
     *
     * @tparam F The type of the callable (lambda, function pointer, functor)
     * @param f The callable to execute on scope exit
     * @return A Deferred object that must be stored to defer execution
     *
     * @note This function is noexcept and constexpr for maximum flexibility
     *
     * Example usage:
     * @code
     *   auto fd = open("file.txt", O_RDONLY);
     *   auto closeFile = defer([fd]() { close(fd); });
     *   // ... use fd ...
     *   // fd is automatically closed when closeFile goes out of scope
     * @endcode
     */
    template<typename F>
    [[nodiscard("The value returned from defer() must not be discarded. Discarding it calls the function passed to defer() right away.")]]
    constexpr Deferred<F> defer(F&& f) noexcept
    {
        return Deferred{std::forward<F>(f)};
    }
}
