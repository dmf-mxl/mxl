// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file platform.h
 * @brief Platform-specific compiler macros and portability helpers for the MXL SDK.
 *
 * This header provides portable definitions that abstract away differences
 * between compilers (GCC, Clang, MSVC, etc.) and between C and C++ language
 * modes. Every other public MXL header includes this file so that the
 * export/visibility annotations and language-level attributes are always
 * available.
 *
 * Macros defined here:
 *   - MXL_EXPORT      : Marks a symbol for export from the shared library.
 *   - MXL_NODISCARD   : Warns callers if they discard the return value.
 *   - MXL_CONSTEXPR   : Maps to `constexpr` in C++ and `inline` in C.
 */

#pragma once

/*
 * ---------------------------------------------------------------------------
 * MXL_EXPORT  --  Shared library symbol visibility
 * ---------------------------------------------------------------------------
 * On GCC and Clang we use the "default" visibility attribute so the linker
 * exports the symbol from the .so / .dylib.  On compilers that do not support
 * this attribute (e.g., MSVC) the macro expands to nothing; Windows builds
 * would need __declspec(dllexport) / __declspec(dllimport) if MXL were to
 * be built as a DLL there.
 */
#if defined(__GNUC__) || defined(__clang__)
#   define MXL_EXPORT __attribute__((visibility("default")))
#else
#   define MXL_EXPORT
#endif

/*
 * ---------------------------------------------------------------------------
 * MXL_NODISCARD / MXL_CONSTEXPR  --  Language-level portability
 * ---------------------------------------------------------------------------
 * These macros let the same header file compile cleanly in both C and C++
 * while still taking advantage of C++ features when available.
 *
 * MXL_NODISCARD : In C++ mode, emits [[nodiscard]] so the compiler warns if
 *                 the return value of a function is silently ignored.  In
 *                 plain C the attribute is not available, so it expands to
 *                 nothing.
 *
 * MXL_CONSTEXPR : In C++ mode, marks a function or variable as constexpr
 *                 (compile-time evaluable).  In C mode, falls back to
 *                 `inline` which is the closest available equivalent.
 */
// TODO: Tailor these more to specific language standard levels
#ifdef __cplusplus
#   define MXL_NODISCARD [[nodiscard]]
#   define MXL_CONSTEXPR constexpr
#else
#   define MXL_NODISCARD
#   define MXL_CONSTEXPR inline
#endif
