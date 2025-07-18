// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(__GNUC__) || defined(__clang__)
#   define MXL_EXPORT __attribute__((visibility("default")))
#else
#   define MXL_EXPORT
#endif

// TODO: Tailor these more to specific language statdard levels
#ifdef __cplusplus
#   define MXL_NODISCARD [[nodiscard]]
#   define MXL_CONSTEXPR constexpr
#else
#   define MXL_NODISCARD
#   define MXL_CONSTEXPR inline
#endif
