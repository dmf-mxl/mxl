// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project https://github.com/dmf-mxl/mxl/contributors.md
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

namespace mxl::tests
{

    std::string readFile(std::filesystem::path const& filepath);
    auto getDomainPath() -> std::filesystem::path;

} // namespace mxl::tests