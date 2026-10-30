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
#ifdef __linux__
            // Ensure the file is large enough to hold the shared data
            if (auto error = ::posix_fallocate(_fd, 0, payloadSize); error != 0)
            {
#elif defined __APPLE__
            if (::ftruncate(_fd, payloadSize) < 0)
            {
                auto const error = errno;
#endif
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

    // The data lives in the file, so there is no section to keep alive.
    SectionPin::~SectionPin() = default;

    SectionPin SharedMemoryBase::pinSection() const
    {
        return SectionPin{};
    }

    StaleFileLock::StaleFileLock(char const* path, std::uint64_t identity) noexcept
    {
        auto const fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd == -1)
        {
            return;
        }

        // Without LOCK_NB flock() waits until no other process holds a lock on the file.
        struct ::stat st;
        if ((::fstat(fd, &st) != 0) || (static_cast<std::uint64_t>(st.st_ino) != identity) || (::flock(fd, LOCK_EX) != 0))
        {
            ::close(fd);
            return;
        }

        // While this process waited, the process that held the lock may have deleted the stale file and published a new flow in its place.
        // Closing the file releases the lock.
        if ((::stat(path, &st) != 0) || (static_cast<std::uint64_t>(st.st_ino) != identity))
        {
            ::close(fd);
            return;
        }

        _fd = fd;
    }

    StaleFileLock::~StaleFileLock()
    {
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

    MXL_EXPORT
    LockProbeResult probeLock(char const* path)
    {
        auto flags = O_RDONLY | O_CLOEXEC;
#ifdef __linux__
        // Do not update the access time of the file. Only the owner of a file may ask for this.
        flags |= O_NOATIME;
#endif
        auto fd = ::open(path, flags);
#ifdef __linux__
        if ((fd == -1) && (errno == EPERM))
        {
            fd = ::open(path, O_RDONLY | O_CLOEXEC);
        }
#endif
        if (fd == -1)
        {
            auto const error = errno;
            if (error == ENOENT)
            {
                return LockProbeResult::NotFound;
            }
            throw std::system_error(error, std::generic_category(), "Could not open shared memory file to probe its lock.");
        }

        // Do not block if the lock cannot be obtained. The lock is released when the file is closed.
        auto const locked = ::flock(fd, LOCK_EX | LOCK_NB) < 0;
        ::close(fd);

        return locked ? LockProbeResult::Locked : LockProbeResult::Unlocked;
    }

    MXL_EXPORT
    void waitWhileExclusivelyLocked(char const* path) noexcept
    {
        auto const fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd == -1)
        {
            return;
        }
        // Without LOCK_NB flock() waits until no other process holds an exclusive lock on the file. Closing the file releases the lock.
        (void)::flock(fd, LOCK_SH);
        ::close(fd);
    }

    MXL_EXPORT
    bool isLockedError(std::system_error const& e) noexcept
    {
        return e.code() == std::errc::operation_would_block;
    }
}
