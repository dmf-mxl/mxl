#include "Utils.hpp"
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>

std::string mxl::tests::readFile(std::filesystem::path const& filepath)
{
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open file: " + filepath.string());
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/// Simple utility function to get the domain path for the tests.
/// This will return a path to /dev/shm/mxl_domain on Linux and a path in the user's
/// home directory ($HOME/mxl_domain) on macOS.
/// \return The path to the mxl domain directory.  The directory may not exist yet.
auto mxl::tests::getDomainPath() -> std::filesystem::path
{
#ifdef __linux__
    return std::filesystem::path{"/dev/shm/mxl_domain"};
#elif __APPLE__
    auto home = std::getenv("HOME");
    if (home)
    {
        return std::filesystem::path{home} / "mxl_domain";
    }
    else
    {
        throw std::runtime_error{"Environment variable HOME is not set."};
    }
#else
#   error "Unsupported platform. This is only implemented for Linux and macOS."
#endif
}