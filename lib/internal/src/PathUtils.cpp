// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PathUtils.hpp"
#include <fmt/format.h>
#include <mxl/platform.h>
#ifdef _WIN32
#   include <windows.h>
#else
#   include <sys/stat.h>
#endif

namespace mxl::lib
{
    MXL_EXPORT
    std::filesystem::path makeFlowDirectoryName(std::filesystem::path const& domain, std::string const& uuid)
    {
        return domain / (uuid + FLOW_DIRECTORY_NAME_SUFFIX);
    }

    MXL_EXPORT
    std::filesystem::path makeFlowDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DATA_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeFlowDescriptorFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_DESCRIPTOR_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeFlowAccessFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / FLOW_ACCESS_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeGrainDirectoryName(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / GRAIN_DIRECTORY_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeGrainDataFilePath(std::filesystem::path const& grainDirectory, unsigned int index)
    {
        return grainDirectory / fmt::format("{}.{}", GRAIN_DATA_FILE_NAME_STEM, index);
    }

    MXL_EXPORT
    std::filesystem::path makeChannelDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / CHANNEL_DATA_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeEventDataFilePath(std::filesystem::path const& flowDirectory)
    {
        return flowDirectory / EVENT_DATA_FILE_NAME;
    }

    MXL_EXPORT
    std::filesystem::path makeDomainOptionsFilePath(std::filesystem::path const& domain)
    {
        return domain / (DOMAIN_OPTIONS_FILE_NAME);
    }

    MXL_EXPORT
    std::optional<FileIdentity> fileIdentity(std::filesystem::path const& path)
    {
#ifdef _WIN32
        // FILE_FLAG_BACKUP_SEMANTICS is needed to open directories. No access rights are needed to read the file index.
        auto const file = ::CreateFileW(
            path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return std::nullopt;
        }

        auto info = BY_HANDLE_FILE_INFORMATION{};
        auto const ok = ::GetFileInformationByHandle(file, &info);
        ::CloseHandle(file);
        if (ok == FALSE)
        {
            return std::nullopt;
        }
        return (static_cast<FileIdentity>(info.nFileIndexHigh) << 32U) | info.nFileIndexLow;
#else
        struct ::stat st;
        if (::stat(path.string().c_str(), &st) != 0)
        {
            return std::nullopt;
        }
        return st.st_ino;
#endif
    }
}
