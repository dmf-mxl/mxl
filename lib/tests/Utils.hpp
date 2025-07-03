#pragma once

#include <filesystem>
#include <string>

namespace mxl::tests
{

    std::string readFile(std::filesystem::path const& filepath);
    auto getDomainPath() -> std::filesystem::path;

} // namespace mxl::tests