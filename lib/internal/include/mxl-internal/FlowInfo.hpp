// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <iosfwd>
#include <mxl/flow.h>
#include <mxl/platform.h>

MXL_EXPORT
std::ostream& operator<<(std::ostream& os, mxlFlowInfo const& obj);
