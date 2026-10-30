// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// The Windows implementation of SharedMemory.hpp. SharedMemory.cpp holds the POSIX one.

#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <winioctl.h>
#include <mxl/platform.h>
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/SharedMemory.hpp"

namespace mxl::lib
{
    namespace
    {
        // flock() locks a whole file. Windows only has byte range locks, which also block reads and writes through file handles to the locked
        // range. Locking a single byte far past the end of any real file gives the flock() semantics without affecting access to the data.
        constexpr auto LOCK_OFFSET_LOW = DWORD{0xFFFFFFFEU};
        constexpr auto LOCK_OFFSET_HIGH = DWORD{0x7FFFFFFFU};

        [[nodiscard]]
        OVERLAPPED lockRange() noexcept
        {
            auto range = OVERLAPPED{};
            range.Offset = LOCK_OFFSET_LOW;
            range.OffsetHigh = LOCK_OFFSET_HIGH;
            return range;
        }

        [[nodiscard]]
        bool tryLock(HANDLE file, bool exclusive) noexcept
        {
            auto range = lockRange();
            auto const flags = DWORD{LOCKFILE_FAIL_IMMEDIATELY} | (exclusive ? DWORD{LOCKFILE_EXCLUSIVE_LOCK} : DWORD{0});
            return ::LockFileEx(file, flags, 0, 1, 0, &range) != FALSE;
        }

        void unlock(HANDLE file) noexcept
        {
            auto range = lockRange();
            (void)::UnlockFileEx(file, 0, 1, 0, &range);
        }

        // ---------------------------------------------------------------------------------------------------------------------------------
        // In-memory sections.
        //
        // Mapping the files of a domain would make the data disk backed: Windows may write dirty pages of a mapped file back to the file at
        // any time, and there is no tmpfs to avoid that. Instead the file only anchors a segment (existence, size, lock, file times, discovery
        // by listing the domain), and the data lives in a named section object backed by RAM and the page file. The section is named after
        // the identity of the file, which survives the rename that publishes a flow directory and is never reused for another file.
        //
        // Sections are visible to the processes of one logon session ("Local" namespace). A flow whose section does not exist, because every
        // process that had it open has exited, is stale.
        // ---------------------------------------------------------------------------------------------------------------------------------

        [[nodiscard]]
        std::system_error lastError(char const* what)
        {
            return std::system_error{static_cast<int>(::GetLastError()), std::system_category(), what};
        }

        [[nodiscard]]
        std::wstring makeSectionName(HANDLE file)
        {
            auto id = FILE_ID_INFO{};
            auto name = std::wstring{L"Local\\mxl-"};
            wchar_t buffer[96];
            if (::GetFileInformationByHandleEx(file, FileIdInfo, &id, sizeof id) != FALSE)
            {
                auto const* const b = reinterpret_cast<unsigned char const*>(&id.FileId);
                auto const* const v = reinterpret_cast<unsigned char const*>(&id.VolumeSerialNumber);
                auto length = 0;
                for (auto i = 0U; i < sizeof id.VolumeSerialNumber; ++i)
                {
                    length += std::swprintf(buffer + length, std::size(buffer) - length, L"%02x", v[i]);
                }
                buffer[length++] = L'-';
                for (auto i = 0U; i < sizeof id.FileId; ++i)
                {
                    length += std::swprintf(buffer + length, std::size(buffer) - length, L"%02x", b[i]);
                }
                return name + std::wstring{buffer, static_cast<std::size_t>(length)};
            }

            auto info = BY_HANDLE_FILE_INFORMATION{};
            if (::GetFileInformationByHandle(file, &info) == FALSE)
            {
                throw lastError("Could not determine the identity of a shared memory file.");
            }
            std::swprintf(buffer,
                std::size(buffer),
                L"%08lx-%08lx%08lx",
                static_cast<unsigned long>(info.dwVolumeSerialNumber),
                static_cast<unsigned long>(info.nFileIndexHigh),
                static_cast<unsigned long>(info.nFileIndexLow));
            return name + buffer;
        }

        // The anchor file never holds data, so do not let it take up disk space.
        void makeSparse(HANDLE file) noexcept
        {
            auto returned = DWORD{};
            (void)::DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr);
        }

        /**
         * Get the identity of an open file, the same value fileIdentity() returns for its path.
         * \return The NTFS file index, or std::nullopt if it cannot be read.
         */
        [[nodiscard]]
        std::optional<std::uint64_t> handleIdentity(HANDLE file) noexcept
        {
            auto info = BY_HANDLE_FILE_INFORMATION{};
            if (::GetFileInformationByHandle(file, &info) == FALSE)
            {
                return std::nullopt;
            }
            return (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U) | info.nFileIndexLow;
        }

        [[nodiscard]]
        HANDLE osHandle(int fd) noexcept
        {
            return reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
        }

        /**
         * Open a file so that it behaves like a POSIX file descriptor: it can be renamed or deleted while open (FILE_SHARE_DELETE).
         * \param[in] path The file path. Narrow paths are UTF-8, as produced by std::filesystem::path::string().
         * \param[in] access The desired access.
         * \param[in] disposition CREATE_NEW or OPEN_EXISTING.
         */
        [[nodiscard]]
        HANDLE openFile(char const* path, DWORD access, DWORD disposition) noexcept
        {
            auto const widePath = std::filesystem::path{path};
            return ::CreateFileW(widePath.c_str(),
                access,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                disposition,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
        }

        /**
         * Set the current time as the last access time, and as the last write time unless the file is only read.
         */
        [[nodiscard]]
        bool touchFile(HANDLE file, bool updateWriteTime) noexcept
        {
            auto now = FILETIME{};
            ::GetSystemTimeAsFileTime(&now);
            return ::SetFileTime(file, nullptr, &now, updateWriteTime ? &now : nullptr) != FALSE;
        }
    }

    MXL_EXPORT
    SharedMemoryBase::SharedMemoryBase(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode)
        : SharedMemoryBase{}
    {
        // FILE_WRITE_ATTRIBUTES lets touch() update the file times of a read only mapping, like futimens() does for the owner of a file.
        auto const readAccess = DWORD{GENERIC_READ | FILE_WRITE_ATTRIBUTES};
        auto const writeAccess = DWORD{GENERIC_READ | GENERIC_WRITE};

        auto file = INVALID_HANDLE_VALUE;
        if (mode == AccessMode::CREATE_READ_WRITE)
        {
            file = openFile(path, writeAccess, CREATE_NEW);
            if (file != INVALID_HANDLE_VALUE)
            {
                makeSparse(file);
                // Ensure the file is large enough to hold the shared data
                auto size = LARGE_INTEGER{};
                size.QuadPart = static_cast<LONGLONG>(payloadSize);
                if ((::SetFilePointerEx(file, size, nullptr, FILE_BEGIN) == FALSE) || (::SetEndOfFile(file) == FALSE))
                {
                    auto const error = lastError("Could not resize shared memory segment.");
                    ::CloseHandle(file);
                    throw error;
                }
                _mode = AccessMode::CREATE_READ_WRITE;
            }
        }

        if (file == INVALID_HANDLE_VALUE)
        {
            if (mode == AccessMode::READ_ONLY)
            {
                file = openFile(path, readAccess, OPEN_EXISTING);
                if ((file == INVALID_HANDLE_VALUE) && (::GetLastError() == ERROR_ACCESS_DENIED))
                {
                    file = openFile(path, GENERIC_READ, OPEN_EXISTING);
                }
            }
            else
            {
                file = openFile(path, writeAccess, OPEN_EXISTING);
            }

            if (file == INVALID_HANDLE_VALUE)
            {
                throw lastError("Could not open shared memory segment.");
            }
            _mode = (mode == AccessMode::READ_ONLY) ? AccessMode::READ_ONLY : AccessMode::READ_WRITE;
        }

        // Wrap the handle in a C runtime file descriptor, which then owns it.
        _fd = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(file), (mode == AccessMode::READ_ONLY) ? _O_RDONLY : _O_RDWR);
        if (_fd == -1)
        {
            ::CloseHandle(file);
            throw std::system_error(errno, std::generic_category(), "Could not open shared memory segment.");
        }

        if (lockMode != LockMode::None && mode != AccessMode::READ_ONLY)
        {
            auto const exclusive = (lockMode == LockMode::Exclusive);
            _lockType = exclusive ? LockType::Exclusive : LockType::Shared;
            if (!tryLock(file, exclusive))
            {
                _lockType = LockType::None;
                throw lastError("Failed to lock shared memory segment.");
            }
        }
        else
        {
            _lockType = LockType::None;
        }

        auto fileSize = LARGE_INTEGER{};
        if (::GetFileSizeEx(file, &fileSize) == FALSE)
        {
            throw lastError("Could not determine size of shared memory segment.");
        }
        if (static_cast<std::size_t>(fileSize.QuadPart) < payloadSize)
        {
            throw std::system_error(ENOMEM, std::generic_category(), "Shared memory segment does not meet the minimum size requirements.");
        }

        // The data lives in an in-memory section named after this file. The segment holds the handle: the name of a section disappears
        // with its last handle, even while views are mapped.
        auto const writable = (mode != AccessMode::READ_ONLY);
        auto const name = makeSectionName(file);
        auto const size = static_cast<ULONGLONG>(fileSize.QuadPart);
        if (_mode == AccessMode::CREATE_READ_WRITE)
        {
            _section = ::CreateFileMappingW(
                INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFFU), name.c_str());
            if ((_section != nullptr) && (::GetLastError() == ERROR_ALREADY_EXISTS))
            {
                // A section by this name can only be left over from a file that no longer exists but whose section is still mapped.
                ::CloseHandle(_section);
                _section = nullptr;
                ::SetLastError(ERROR_ALREADY_EXISTS);
            }
            if (_section == nullptr)
            {
                throw lastError("Could not create the in-memory section of a shared memory segment.");
            }
        }
        else
        {
            _section = ::OpenFileMappingW(writable ? (FILE_MAP_READ | FILE_MAP_WRITE) : FILE_MAP_READ, FALSE, name.c_str());
            if (_section == nullptr)
            {
                auto const error = static_cast<int>(::GetLastError());
                if (error == ERROR_FILE_NOT_FOUND)
                {
                    throw SectionMissingError(error, "The in-memory section of a shared memory segment is gone.", handleIdentity(file).value_or(0));
                }
                throw std::system_error(error, std::system_category(), "Could not open the in-memory section of a shared memory segment.");
            }
        }

        auto const view = ::MapViewOfFile(_section, writable ? (FILE_MAP_READ | FILE_MAP_WRITE) : FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(size));
        if (view == nullptr)
        {
            throw lastError("Could not map shared memory segment.");
        }

        _data = view;
        _mappedSize = static_cast<std::size_t>(fileSize.QuadPart);
    }

    MXL_EXPORT
    SharedMemoryBase::~SharedMemoryBase()
    {
        if (_data != nullptr)
        {
            (void)::UnmapViewOfFile(_data);
        }

        if (_section != nullptr)
        {
            (void)::CloseHandle(_section);
        }

        if (_fd != -1)
        {
            if (_lockType != LockType::None)
            {
                unlock(osHandle(_fd));
            }
            (void)::_close(_fd);
        }
    }

    SectionPin::~SectionPin()
    {
        if (_handle != nullptr)
        {
            (void)::CloseHandle(_handle);
        }
    }

    StaleFileLock::StaleFileLock(char const* path, std::uint64_t identity) noexcept
    {
        auto const file = openFile(path, GENERIC_READ, OPEN_EXISTING);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        // Without LOCKFILE_FAIL_IMMEDIATELY this waits until no other process holds a lock on the file.
        auto range = lockRange();
        if ((handleIdentity(file) != identity) || (::LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &range) == FALSE))
        {
            ::CloseHandle(file);
            return;
        }

        // While this process waited, the process that held the lock may have deleted the stale file and published a new flow in its place.
        auto const current = openFile(path, 0, OPEN_EXISTING);
        auto const currentIdentity = (current != INVALID_HANDLE_VALUE) ? handleIdentity(current) : std::nullopt;
        if (current != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(current);
        }
        if (currentIdentity != identity)
        {
            unlock(file);
            ::CloseHandle(file);
            return;
        }

        // Wrap the handle in a C runtime file descriptor, which then owns it.
        _fd = ::_open_osfhandle(reinterpret_cast<std::intptr_t>(file), _O_RDONLY);
        if (_fd == -1)
        {
            unlock(file);
            ::CloseHandle(file);
        }
    }

    StaleFileLock::~StaleFileLock()
    {
        if (_fd != -1)
        {
            unlock(osHandle(_fd));
            (void)::_close(_fd);
        }
    }

    SectionPin SharedMemoryBase::pinSection() const
    {
        auto duplicate = HANDLE{};
        if ((_section == nullptr) ||
            (::DuplicateHandle(::GetCurrentProcess(), _section, ::GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE))
        {
            return SectionPin{};
        }
        return SectionPin{duplicate};
    }

    std::wstring SharedMemoryBase::sectionName() const
    {
        return makeSectionName(osHandle(_fd));
    }

    void SharedMemoryBase::touch()
    {
        if (!touchFile(osHandle(_fd), accessMode() != AccessMode::READ_ONLY))
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

        if (_fd == -1)
        {
            throw std::runtime_error("Tried to upgrade a lock of an invalid shared memory segment");
        }

        // A byte range lock cannot be converted. Release the shared lock, try the exclusive one, and take the shared lock back if that fails.
        // Another process can take a lock in between, which then makes this upgrade fail. flock() converts a lock the same way.
        auto const file = osHandle(_fd);
        unlock(file);
        if (!tryLock(file, true))
        {
            auto const error = ::GetLastError();
            if (!tryLock(file, false))
            {
                _lockType = LockType::None;
                MXL_ERROR("Failed to restore the shared lock of a shared memory file after a failed upgrade.");
            }
            if (error == ERROR_LOCK_VIOLATION)
            {
                // Another process (or this process on another handle) is still holding a shared lock on the file
                return false;
            }
            throw std::system_error(static_cast<int>(error), std::system_category(), "Failed to upgrade lock of shared memory file");
        }

        _lockType = LockType::Exclusive;
        return true;
    }

    MXL_EXPORT
    LockProbeResult probeLock(char const* path)
    {
        auto const file = openFile(path, GENERIC_READ, OPEN_EXISTING);
        if (file == INVALID_HANDLE_VALUE)
        {
            auto const error = ::GetLastError();
            if ((error == ERROR_FILE_NOT_FOUND) || (error == ERROR_PATH_NOT_FOUND))
            {
                return LockProbeResult::NotFound;
            }
            throw std::system_error(static_cast<int>(error), std::system_category(), "Could not open shared memory file to probe its lock.");
        }

        auto const locked = !tryLock(file, true);
        if (!locked)
        {
            unlock(file);
        }
        ::CloseHandle(file);

        return locked ? LockProbeResult::Locked : LockProbeResult::Unlocked;
    }

    MXL_EXPORT
    void waitWhileExclusivelyLocked(char const* path) noexcept
    {
        auto const file = openFile(path, GENERIC_READ, OPEN_EXISTING);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        // Without LOCKFILE_FAIL_IMMEDIATELY this waits until no other process holds an exclusive lock on the file.
        auto range = lockRange();
        if (::LockFileEx(file, 0, 0, 1, 0, &range) != FALSE)
        {
            unlock(file);
        }
        ::CloseHandle(file);
    }

    MXL_EXPORT
    bool isLockedError(std::system_error const& e) noexcept
    {
        return (e.code().category() == std::system_category()) && (e.code().value() == ERROR_LOCK_VIOLATION);
    }
}
