// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/SharedMemory.hpp"
#include <cerrno>
#include <cstring>
#include <array>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <mxl/platform.h>
#include "mxl-internal/Logging.hpp"

namespace mxl::lib
{
    MXL_EXPORT
    SharedMemoryBase::SharedMemoryBase(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode)
        : SharedMemoryBase{}
    {
        constexpr auto const OMODE_CREATE = O_EXCL | O_CREAT | O_RDWR | O_CLOEXEC;
        constexpr auto const OMODE_RO = O_RDONLY | O_CLOEXEC;
        constexpr auto const OMODE_RW = O_RDWR | O_CLOEXEC;

        if ((mode == AccessMode::CREATE_READ_WRITE) && ((_fd = ::open(path, OMODE_CREATE, 0664)) != -1))
        {
            // Ensure the file is large enough to hold the shared data
            if (auto error = ::posix_fallocate(_fd, 0, payloadSize); error != 0)
            {
                // No need to close _fd, as the destructor *will* be called,
                // because we delegated to the default constructor.
                throw std::system_error(error, std::generic_category(), "Could not resize shared memory segment.");
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

        if (lockMode != LockMode::None && mode != AccessMode::READ_ONLY)
        {
            auto lockFlags = LOCK_NB;

            if (lockMode == LockMode::Exclusive)
            {
                lockFlags |= LOCK_EX;
                _lockType = LockType::Exclusive;
            }
            else
            {
                lockFlags |= LOCK_SH;
                _lockType = LockType::Shared;
            }

            if (::flock(_fd, lockFlags) == -1)
            {
                throw std::system_error{errno, std::system_category(), fmt::format("Failed to lock shared memory segment.")};
            }
        }
        else
        {
            _lockType = LockType::None;
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

    MXL_EXPORT
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

    bool SharedMemoryBase::makeExclusive()
    {
        if (_lockType == LockType::Exclusive)
        {
            // This is a no-op
            return true;
        }

        if (::flock(_fd, LOCK_EX | LOCK_NB) < 0)
        {
            auto const error = errno;
            switch (error)
            {
                case EWOULDBLOCK:
                    // Another process (or this process on another fd) is still holding a shared lock on the file
                    return false;
                case EBADF: throw std::runtime_error("Tried to upgrade a lock of an invalid shared memory segment");
                default:    throw std::system_error(error, std::system_category(), fmt::format("Failed to upgrade lock of shared memory file"));
            }
        }

        _lockType = LockType::Exclusive;
        return true;
    }
}
