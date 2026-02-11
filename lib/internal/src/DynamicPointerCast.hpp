// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file DynamicPointerCast.hpp
 * @brief Provides dynamic_cast functionality for std::unique_ptr
 *
 * The C++ standard library provides std::dynamic_pointer_cast for std::shared_ptr but not
 * for std::unique_ptr. This file fills that gap by providing a type-safe dynamic cast
 * operation that transfers ownership when the cast succeeds.
 *
 * Design rationale:
 * - Maintains unique ownership semantics (only one owner at a time)
 * - Returns nullptr on failed cast without releasing the source pointer
 * - Only releases source pointer on successful cast to prevent resource leaks
 * - noexcept guarantee since dynamic_cast itself is noexcept
 *
 * This is particularly useful when working with polymorphic flow types (discrete vs continuous)
 * where the concrete type needs to be determined at runtime.
 */

#pragma once

#include <memory>

namespace mxl::lib
{
    /**
     * @brief Perform a dynamic cast on a unique_ptr, transferring ownership on success
     *
     * This function attempts to cast a unique_ptr<From> to unique_ptr<To> using dynamic_cast.
     * If the cast succeeds, ownership is transferred to the returned pointer and the source
     * is released. If the cast fails, the source pointer retains ownership and nullptr is returned.
     *
     * This is the unique_ptr equivalent of std::dynamic_pointer_cast for shared_ptr.
     *
     * @tparam To The target type to cast to (must be related to From via inheritance)
     * @tparam From The source type (base or derived class)
     * @param source The unique_ptr to cast (moved from, potentially released)
     * @return unique_ptr<To> containing the casted pointer on success, nullptr on failure
     *
     * @note If the cast fails, the source unique_ptr still owns its object
     * @note If the cast succeeds, the source unique_ptr is emptied (ownership transferred)
     * @note This function is noexcept - it never throws exceptions
     *
     * Example usage:
     * @code
     *   std::unique_ptr<FlowData> flowData = openFlow(id);
     *   if (auto discreteData = dynamic_pointer_cast<DiscreteFlowData>(std::move(flowData)); discreteData) {
     *       // Use discreteData (ownership transferred)
     *   } else {
     *       // flowData still owns the object (cast failed)
     *   }
     * @endcode
     */
    template<typename To, typename From>
    std::unique_ptr<To> dynamic_pointer_cast(std::unique_ptr<From>&& source) noexcept
    {
        // Attempt the dynamic cast on the raw pointer
        auto const p = dynamic_cast<To*>(source.get());

        // Only release ownership if the cast succeeded
        if (p != nullptr)
        {
            source.release();
        }

        // Return the casted pointer (nullptr if cast failed, preserving source ownership)
        return std::unique_ptr<To>{p};
    }
}
