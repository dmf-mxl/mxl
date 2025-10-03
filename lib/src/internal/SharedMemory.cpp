// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "SharedMemory.hpp"
#include <cerrno>
#include <cstring>
#include <array>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include "Logging.hpp"

namespace mxl::lib
{
    SharedMemoryBase::SharedMemoryBase(char const* path, AccessMode mode, std::size_t payloadSize)
        : SharedMemoryBase{}
    {
        constexpr auto const OMODE_CREATE = O_EXCL | O_CREAT | O_RDWR | O_CLOEXEC;
        constexpr auto const OMODE_RO = O_RDONLY | O_CLOEXEC;
        constexpr auto const OMODE_RW = O_RDWR | O_CLOEXEC;

        if ((mode == AccessMode::CREATE_READ_WRITE) && ((_fd = ::open(path, OMODE_CREATE, 0664)) != -1))
        {
            // Ensure the file is large enough to hold the shared data
            if (::ftruncate(_fd, payloadSize) == -1)
            {
                // No need to close _fd, as the destructor *will* be called,
                // because we delegated to the default constructor.
                throw std::system_error(errno, std::generic_category(), "Could not resize shared memory segment.");
            }
            _mode = AccessMode::CREATE_READ_WRITE;
        }
        else
        {
            if ((_fd = ::open(path, (mode == AccessMode::READ_ONLY) ? OMODE_RO : OMODE_RW)) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Could not open shared memory segment.");
            }
            _mode = (mode == AccessMode::READ_ONLY) ? AccessMode::READ_ONLY : AccessMode::READ_WRITE;
        }

        if (mode != AccessMode::READ_ONLY)
        {
            // Try to obtain a shared lock on the the file descriptor
            // This is useful to detect if ringbufers and flows are 'stale' (if the writer processes are still using it)
            if (::flock(_fd, LOCK_SH | LOCK_NB) == -1)
            {
                [[maybe_unused]]
                auto const errorNumber = errno;

                MXL_WARN("Failed to aquire a shared advisory lock on file: {}, {}", path, std::strerror(errorNumber));
            }
        }

        if (struct stat statBuf; ::fstat(_fd, &statBuf) != -1)
        {
            if (static_cast<std::size_t>(statBuf.st_size) >= payloadSize)
            {
                auto const shared_data_buffer = ::mmap(
                    nullptr, statBuf.st_size, PROT_READ | ((mode != AccessMode::READ_ONLY) ? PROT_WRITE : 0), MAP_FILE | MAP_SHARED, _fd, 0);
                if (shared_data_buffer != MAP_FAILED)
                {
                    _data = shared_data_buffer;
                    _mappedSize = statBuf.st_size;
                }
                else
                {
                    throw std::system_error(errno, std::generic_category(), "Could not map shared memory segment.");
                }
            }
            else
            {
                throw std::system_error(ENOMEM, std::generic_category(), "Shared memory segment does not meet the minimum size requirements.");
            }
        }
        else
        {
            throw std::system_error(errno, std::generic_category(), "Could not determine size of shared memory segment.");
        }
    }

    SharedMemoryBase::~SharedMemoryBase()
    {
        if (_data != nullptr)
        {
            (void)::munmap(_data, _mappedSize);
        }

        if (_fd != -1)
        {
            (void)::flock(_fd, LOCK_UN);
            (void)::close(_fd);
        }
    }

    void SharedMemoryBase::touch()
    {
        // Update the file times
        auto const times = std::array<std::timespec, 2>{
            {{0, UTIME_NOW}, {0, (accessMode() == AccessMode::READ_ONLY) ? UTIME_OMIT : UTIME_NOW}}
        };

        if (::futimens(_fd, times.data()) == -1)
        {
            MXL_ERROR("Failed to update file times");
        }
    }
}
