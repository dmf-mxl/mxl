// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace mxl::tests
{
    /// Sets or clears MXL_FABRICS_NOTIFY for the lifetime of the object and restores the previous value afterwards.
    class NotifyEnvGuard
    {
    public:
        /** \brief Save the current value of MXL_FABRICS_NOTIFY and replace it.
         * \param[in] value The new value, or nullptr to unset the variable.
         */
        explicit NotifyEnvGuard(char const* value)
        {
            if (auto const* previous = std::getenv(name); previous != nullptr)
            {
                _previous = previous;
            }
            set(value);
        }

        /// Restore the value MXL_FABRICS_NOTIFY had before construction.
        ~NotifyEnvGuard()
        {
            set(_previous ? _previous->c_str() : nullptr);
        }

        NotifyEnvGuard(NotifyEnvGuard const&) = delete;
        NotifyEnvGuard& operator=(NotifyEnvGuard const&) = delete;

    private:
        /** \brief Set or unset MXL_FABRICS_NOTIFY.
         * \param[in] value The new value, or nullptr to unset the variable.
         */
        static void set(char const* value)
        {
            if (value != nullptr)
            {
                ::setenv(name, value, 1);
            }
            else
            {
                ::unsetenv(name);
            }
        }

        constexpr static char const* name = "MXL_FABRICS_NOTIFY"; ///< The environment variable read by the fabrics library.
        std::optional<std::string> _previous;                     ///< Value before construction, or std::nullopt if the variable was unset.
    };
}
