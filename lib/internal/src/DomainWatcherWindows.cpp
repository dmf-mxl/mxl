// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

// The Windows parts of the DomainWatcher. DomainWatcher.cpp holds the common code and the Linux and macOS parts.

#include <cstddef>
#include <array>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <windows.h>
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/Timing.hpp"

namespace mxl::lib
{
    void DomainWatcher::openDirectoryWatch(std::filesystem::path const& domain)
    {
        // Readers set the last write time of their access file. The directory is opened for overlapped reads, so the watcher thread can
        // wait for changes and for a stop request at the same time.
        _directory = ::CreateFileW(domain.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);
        if (_directory == INVALID_HANDLE_VALUE)
        {
            auto const error = static_cast<int>(::GetLastError());
            throw std::system_error(error, std::system_category(), "Failed to open the domain directory for watching");
        }

        _stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (_stopEvent == nullptr)
        {
            auto const error = static_cast<int>(::GetLastError());
            ::CloseHandle(_directory);
            throw std::system_error(error, std::system_category(), "Failed to create the DomainWatcher stop event");
        }
    }

    void DomainWatcher::closeDirectoryWatch() noexcept
    {
        ::CloseHandle(_directory);
        ::CloseHandle(_stopEvent);
    }

    void DomainWatcher::watchDirectory()
    {
        // FILE_NOTIFY_INFORMATION records are DWORD aligned.
        alignas(DWORD) std::array<std::byte, 16384> buffer{};

        auto overlapped = OVERLAPPED{};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr)
        {
            MXL_ERROR("Failed to create the DomainWatcher event: error {}", ::GetLastError());
            return;
        }
        auto const events = std::array<HANDLE, 2>{_stopEvent, overlapped.hEvent};

        while (_running)
        {
            ::ResetEvent(overlapped.hEvent);
            if (::ReadDirectoryChangesW(
                    _directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE, nullptr, &overlapped, nullptr) ==
                FALSE)
            {
                MXL_ERROR("ReadDirectoryChangesW failed: error {}", ::GetLastError());
                break;
            }

            auto length = DWORD{0};
            if (::WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, INFINITE) != (WAIT_OBJECT_0 + 1))
            {
                // Stop requested. Wait for the cancelled read to finish, because it writes into the buffer.
                ::CancelIoEx(_directory, &overlapped);
                (void)::GetOverlappedResult(_directory, &overlapped, &length, TRUE);
                break;
            }

            if (::GetOverlappedResult(_directory, &overlapped, &length, FALSE) == FALSE)
            {
                auto const error = ::GetLastError();
                if (error == ERROR_ACCESS_DENIED)
                {
                    // The domain directory was deleted, so there is nothing left to watch.
                    MXL_DEBUG("The watched domain directory was removed.");
                }
                else
                {
                    MXL_ERROR("Error reading directory changes: error {}", error);
                }
                break;
            }

            // A length of 0 means the changes did not fit into the buffer and were dropped. The next read sees new changes again.
            if (length > 0)
            {
                processNotifications(buffer.data(), length);
            }
        }

        ::CloseHandle(overlapped.hEvent);
    }

    void DomainWatcher::signalStop() noexcept
    {
        if (::SetEvent(_stopEvent) == FALSE)
        {
            MXL_ERROR("Failed to signal DomainWatcher stop request: error {}", ::GetLastError());
        }
    }

    void DomainWatcher::processNotifications(void const* buffer, std::size_t length)
    {
        auto lock = std::lock_guard{_mutex};
        auto const time = currentTime(Clock::TAI);
        auto const* const bytes = static_cast<std::byte const*>(buffer);

        for (auto offset = std::size_t{0}; offset < length;)
        {
            auto const* const info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(bytes + offset);

            // Names are relative to the domain, for example "<flow id>.mxl-flow\access".
            auto const name = std::filesystem::path{
                std::wstring{info->FileName, info->FileNameLength / sizeof(WCHAR)}
            };
            if (name.filename() == FLOW_ACCESS_FILE_NAME)
            {
                auto const flowDirectory = name.parent_path().string();
                for (auto& [wd, record] : _watches)
                {
                    if ((uuids::to_string(record.id) + FLOW_DIRECTORY_NAME_SUFFIX) != flowDirectory)
                    {
                        continue;
                    }

                    try
                    {
                        updateReadTime(record.flowData->get()->info, time.value);
                    }
                    catch (std::exception const& e)
                    {
                        MXL_ERROR("Exception in DomainWatcher callback: {}", e.what());
                    }
                }
            }

            if (info->NextEntryOffset == 0)
            {
                break;
            }
            offset += info->NextEntryOffset;
        }
    }
}
