// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <string>

namespace mxl::tests
{
    //
    // RAII helper to prepare and cleanup a domain for the duration of a test
    //
    class mxlDomainFixture
    {
    public:
        /// Create the fixture. Will delete the domain if it exists and create a new one.
        mxlDomainFixture();
        /// Delete the domain folder
        ~mxlDomainFixture();

    protected:
        /// The path to the domain
        std::filesystem::path domain;

    private:
        /// Remove the domain folder if it exists
        void removeDomain();
    };

    // Simple utility to read a file into a string
    std::string readFile(std::filesystem::path const& filepath);

    // Helper to get the domain path for the tests
    auto getDomainPath() -> std::filesystem::path;

    // Helper to make a unique temp domain
    auto makeTempDomain() -> std::filesystem::path;

} // namespace mxl::tests