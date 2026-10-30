// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "FlowAccessFile.hpp"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <array>
#include <fcntl.h>
#ifdef _WIN32
#   include <io.h>
#   include <windows.h>
#else
#   include <unistd.h>
#   include <sys/stat.h>
#endif
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    namespace
    {
#ifdef _WIN32
        int openFile(std::filesystem::path const& path) noexcept
        {
            // Just enough rights to set the file times.
            auto const file = ::CreateFileW(path.c_str(),
                FILE_WRITE_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return -1;
            }

            // Wrap the handle in a C runtime file descriptor, which then owns it.
            auto const fd = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(file), _O_RDONLY);
            if (fd == -1)
            {
                ::CloseHandle(file);
            }
            return fd;
        }

        bool touchFile(int fd) noexcept
        {
            // The DomainWatcher on Windows is notified of changes to the last write time. Windows does not report updates of the last
            // access time reliably.
            auto now = FILETIME{};
            ::GetSystemTimeAsFileTime(&now);
            return ::SetFileTime(reinterpret_cast<HANDLE>(::_get_osfhandle(fd)), nullptr, nullptr, &now) != FALSE;
        }
#else
        int openFile(std::filesystem::path const& path) noexcept
        {
            return ::open(path.c_str(), O_RDWR | O_CLOEXEC);
        }

        bool touchFile(int fd) noexcept
        {
            auto const times = std::array<timespec, 2>{
                {{.tv_sec = 0, .tv_nsec = UTIME_NOW}, {.tv_sec = 0, .tv_nsec = UTIME_OMIT}}
            };
            return (::futimens(fd, times.data()) == 0);
        }
#endif
    }

    FlowAccessFile::FlowAccessFile(std::filesystem::path const& path) noexcept
        : _fd{openFile(path)}
    {}

    FlowAccessFile::~FlowAccessFile()
    {
        if ((_fd != -1) && (::close(_fd) != 0))
        {
            auto const error = errno;
            MXL_ERROR("Failed to close access file fd: {}", std::strerror(error));
        }
    }

    bool FlowAccessFile::touch() const noexcept
    {
        return (_fd != -1) && touchFile(_fd);
    }
}
