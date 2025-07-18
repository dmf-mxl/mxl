// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ctime>

bool operator==(timespec const& lhs, timespec const& rhs);

bool operator!=(timespec const& lhs, timespec const& rhs);

bool operator<(timespec const& lhs, timespec const& rhs);

bool operator<=(timespec const& lhs, timespec const& rhs);

bool operator>(timespec const& lhs, timespec const& rhs);

bool operator>=(timespec const& lhs, timespec const& rhs);