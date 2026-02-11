// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file Utils.hpp
 * @brief Test utility functions and fixtures for MXL unit tests
 *
 * This header provides common test infrastructure used across all MXL test suites:
 *   - Domain path selection (Linux: /dev/shm, macOS: $HOME)
 *   - RAII domain fixture for automatic setup/teardown
 *   - File reading utilities for loading test data (NMOS flow descriptors)
 *   - Temporary domain creation for isolated testing
 *
 * The mxlDomainFixture ensures each test starts with a clean domain and
 * automatically cleans up afterward, preventing test interference.
 */

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

    /**
     * @brief Read entire file contents into a string
     * @param filepath Path to the file to read
     * @return File contents as string
     * @throws std::runtime_error if file cannot be opened
     */
    std::string readFile(std::filesystem::path const& filepath);

    /**
     * @brief Get the default MXL domain path for tests
     *
     * Returns platform-specific shared memory location:
     *   - Linux: /dev/shm/mxl_domain (tmpfs for zero-copy performance)
     *   - macOS: $HOME/mxl_domain (macOS doesn't expose tmpfs to userspace)
     *
     * @return The path to the MXL domain directory (may not exist yet)
     * @throws std::runtime_error on unsupported platforms or missing $HOME
     */
    std::filesystem::path getDomainPath();

    /**
     * @brief Create a unique temporary domain for isolated testing
     *
     * Uses mkdtemp to create a unique domain directory with random suffix.
     * Useful for parallel test execution without domain conflicts.
     *
     * @return Path to the newly created temporary domain
     * @throws std::runtime_error if directory creation fails
     */
    std::filesystem::path makeTempDomain();

    /**
     * @brief RAII test fixture for MXL domain lifecycle management
     *
     * This fixture ensures clean test isolation by:
     *   1. Removing any existing domain before test starts
     *   2. Creating a fresh domain directory
     *   3. Automatically cleaning up domain when test completes
     *
     * Usage with Catch2:
     *   TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Test name", "[tag]")
     *   {
     *       // 'domain' member is available here
     *       auto instance = mxlCreateInstance(domain.string().c_str(), "{}");
     *       // ... test code ...
     *   }
     *   // Domain is automatically cleaned up when test exits
     */
    class mxlDomainFixture
    {
    public:
        /**
         * @brief Constructor: remove old domain if present, create fresh one
         */
        mxlDomainFixture()
            : domain{getDomainPath()}
        {
            removeDomain();
            std::filesystem::create_directories(domain);
        }

        /**
         * @brief Destructor: clean up domain directory and all flows
         */
        ~mxlDomainFixture()
        {
            removeDomain();
        }

    protected:
        /** @brief The path to the MXL domain being tested */
        std::filesystem::path domain;

        /**
         * @brief Check if a flow directory exists in the domain
         * @param id Flow UUID string
         * @return true if the flow's .mxl-flow directory exists
         */
        [[nodiscard]]
        bool flowDirectoryExists(std::string const& id) const
        {
            return std::filesystem::exists(lib::makeFlowDirectoryName(domain, id));
        }

    private:
        /**
         * @brief Remove the domain directory if it exists
         */
        void removeDomain()
        {
            if (std::filesystem::exists(domain))
            {
                std::filesystem::remove_all(domain);
            }
        }
    };

    /**************************************************************************/
    /* Inline implementation                                                  */
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
#else
#   error "Unsupported platform. This is only implemented for Linux and macOS."
#endif
    }

    inline std::filesystem::path makeTempDomain()
    {
#ifdef __linux__
        char tmpl[] = "/dev/shm/mxl_test_domainXXXXXX";
#elif __APPLE__
        char tmpl[] = "/tmp/mxl_test_domainXXXXXX";
#else
#   error "Unsupported platform. This is only implemented for Linux and macOS."
#endif
        if (::mkdtemp(tmpl) == nullptr)
        {
            throw std::runtime_error("Failed to create temporary directory");
        }
        return std::filesystem::path{tmpl};
    }

} // namespace mxl::tests
