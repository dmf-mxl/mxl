// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "mxl-internal/PathUtils.hpp"

#ifdef __APPLE__
#   include <stdexcept>
#   include <unistd.h> // For mkdtemp
#endif

namespace mxl::tests
{

    std::string readFile(std::filesystem::path const& filepath);

    /**
     * Simple utility function to get the domain path for the tests.
     * This will return a path to /dev/shm/mxl_domain on Linux and a path in the user's
     * home directory ($HOME/mxl_domain) on macOS.
     * \return The path to the mxl domain directory.  The directory may not exist yet.
     */
    std::filesystem::path getDomainPath();

    // Helper to make a unique temp domain
    std::filesystem::path makeTempDomain();

    //
    // RAII helper to prepare and cleanup a domain for the duration of a test
    //
    class mxlDomainFixture
    {
    public:
        /// Create the fixture. Will delete the domain if it exists and create a new one.
        mxlDomainFixture()
            : domain{getDomainPath()}
        {
            removeDomain();
            std::filesystem::create_directories(domain);
        }

        /// Delete the domain folder
        ~mxlDomainFixture()
        {
            removeDomain();
        }

    protected:
        /// The path to the domain
        std::filesystem::path domain;

        [[nodiscard]]
        bool flowDirectoryExists(std::string const& id) const
        {
            return std::filesystem::exists(lib::makeFlowDirectoryName(domain, id));
        }

    private:
        /// Remove the domain folder if it exists
        void removeDomain()
        {
            if (std::filesystem::exists(domain))
            {
                std::filesystem::remove_all(domain);
            }
        }
    };

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/
    inline std::string readFile(std::filesystem::path const& filepath)
    {
        if (auto file = std::ifstream{filepath, std::ios::in | std::ios::binary}; file)
        {
            auto reader = std::stringstream{};
            reader << file.rdbuf();
            return reader.str();
        }

        throw std::runtime_error("Failed to open file: " + filepath.string());
    }

    inline std::filesystem::path getDomainPath()
    {
#ifdef __linux__
        return std::filesystem::path{"/dev/shm/mxl_domain"};
#elif __APPLE__
        if (auto const home = std::getenv("HOME"); home != nullptr)
        {
            return std::filesystem::path{home} / "mxl_domain";
        }
        else
        {
            throw std::runtime_error{"Environment variable HOME is not set."};
        }
#elif defined _WIN32
        return std::filesystem::temp_directory_path() / "mxl_domain";
#else
#   error "Unsupported platform. This is only implemented for Linux, macOS and Windows."
#endif
    }

    inline std::filesystem::path makeTempDomain()
    {
#ifdef __linux__
        char tmpl[] = "/dev/shm/mxl_test_domainXXXXXX";
#elif __APPLE__
        char tmpl[] = "/tmp/mxl_test_domainXXXXXX";
#elif defined _WIN32
        auto tmplString = (std::filesystem::temp_directory_path() / "mxl_test_domainXXXXXX").string();
        auto* const tmpl = tmplString.data();
#else
#   error "Unsupported platform. This is only implemented for Linux, macOS and Windows."
#endif
        if (::mkdtemp(tmpl) == nullptr)
        {
            throw std::runtime_error("Failed to create temporary directory");
        }
        return std::filesystem::path{tmpl};
    }

} // namespace mxl::tests
