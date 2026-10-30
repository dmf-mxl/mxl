// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <mxl-internal/Logging.hpp>
#include <mxl/platform.h>

namespace mxl::lib
{
    enum class LockMode
    {
        Exclusive,
        Shared,
        None,
    };

    enum class AccessMode
    {
        READ_ONLY,
        READ_WRITE,
        CREATE_READ_WRITE
    };

    /**
     * Result of probing the lock of a shared memory file.
     */
    enum class LockProbeResult
    {
        Unlocked, ///< No shared memory instance holds a lock on the file.
        Locked,   ///< At least one shared memory instance, in any process, holds a lock on the file.
        NotFound, ///< The file does not exist.
    };

    /**
     * Probe whether any shared memory instance holds a lock on a file, by trying to take an exclusive lock without blocking.
     * \param[in] path Path of the shared memory file.
     * \return The probe result.
     * \throw std::system_error If the file exists but cannot be opened.
     */
    MXL_EXPORT
    LockProbeResult probeLock(char const* path);

    /**
     * Wait until no process holds an exclusive lock on a file. Only a process that deletes a flow holds one, so this waits for the deletion.
     * Returns at once if the file does not exist or cannot be opened.
     * \param[in] path Path of the shared memory file.
     */
    MXL_EXPORT
    void waitWhileExclusivelyLocked(char const* path) noexcept;

    /** \return Whether an error means that a shared memory file could not be locked because another process holds an exclusive lock on it. */
    MXL_EXPORT
    bool isLockedError(std::system_error const& e) noexcept;

    /**
     * Whether a directory can be renamed while files in it are open. Windows cannot, so a new flow is closed while its directory is
     * renamed to publish it, and opened again afterwards.
     */
#ifdef _WIN32
    constexpr auto canRenameDirectoryWithOpenFiles = false;
#else
    constexpr auto canRenameDirectoryWithOpenFiles = true;
#endif

    /**
     * Thrown when the in-memory section of a segment no longer exists: every process that had the flow open has closed it (or exited), so the
     * data is gone and only the anchor file in the domain is left. The flow is stale. Only thrown on Windows, where the data of a segment does not
     * live in its file.
     */
    class MXL_EXPORT SectionMissingError : public std::system_error
    {
    public:
        /**
         * \param[in] error The Windows error code.
         * \param[in] what The message.
         * \param[in] fileIdentity The identity of the anchor file whose section is gone, as returned by fileIdentity().
         */
        SectionMissingError(int error, char const* what, std::uint64_t fileIdentity)
            : std::system_error{error, std::system_category(), what}
            , _fileIdentity{fileIdentity}
        {}

        /** \return The identity of the anchor file whose section is gone. */
        [[nodiscard]]
        std::uint64_t fileIdentity() const noexcept
        {
            return _fileIdentity;
        }

    private:
        /** The identity of the anchor file whose section is gone. */
        std::uint64_t _fileIdentity;
    };

    /**
     * An exclusive lock on the file of a stale segment, held until the lock is destroyed.
     *
     * Several processes can find the same stale flow and replace it at the same time. Only the process that holds this lock on the flow data
     * file deletes the flow. A process that waited for the lock finds that the path names the file of the new flow by then, and leaves it alone.
     * Stale flows only exist on Windows, but the lock works the same way on every platform.
     *
     * Waiting for the lock has no timeout. Nobody maps a stale file, so the only other locks on it are taken without waiting and released
     * right away by processes that try to open the flow, and this lock by another replacer, which holds it while it deletes the flow.
     */
    class MXL_EXPORT StaleFileLock
    {
    public:
        /**
         * Lock the file at a path exclusively if it is the given stale file. Waits while another process holds a lock on the file.
         * The lock is empty if the path cannot be opened or does not name that file, before or after waiting.
         * \param[in] path Path of the file.
         * \param[in] identity The identity of the stale file, from SectionMissingError::fileIdentity().
         */
        StaleFileLock(char const* path, std::uint64_t identity) noexcept;

        StaleFileLock(StaleFileLock const&) = delete;
        StaleFileLock& operator=(StaleFileLock const&) = delete;

        /** Release the lock and close the file. */
        ~StaleFileLock();

        /** \return Whether the lock is held. */
        explicit operator bool() const noexcept
        {
            return _fd != -1;
        }

    private:
        /** The file descriptor of the locked file, or -1. */
        int _fd = -1;
    };

    /**
     * Keeps the in-memory section of a shared memory segment alive after the segment object is gone.
     *
     * On Windows the data of a segment lives in a named section object backed by RAM (and the system page file), not by the
     * file in the MXL domain, which only anchors the segment for discovery, locking and cleanup. A section disappears with
     * its last handle, but a directory cannot be renamed while files inside it are open. Flow creation therefore pins the
     * sections, closes the flow files, publishes the directory and reopens the flow, which opens the sections again.
     *
     * On other platforms the data lives in the file itself, and a pin is always empty.
     */
    class MXL_EXPORT SectionPin
    {
    public:
        /** An empty pin that keeps nothing alive. */
        SectionPin() noexcept = default;

        /**
         * Take ownership of a section handle.
         * \param[in] sectionHandle A handle of the section, which the pin closes.
         */
        explicit SectionPin(void* sectionHandle) noexcept
            : _handle{sectionHandle}
        {}

        /** Take over the handle of another pin, which becomes empty. */
        SectionPin(SectionPin&& other) noexcept
            : _handle{std::exchange(other._handle, nullptr)}
        {}

        /** Swap the handles of the two pins. The handle this pin held is closed with the other pin. */
        SectionPin& operator=(SectionPin&& other) noexcept
        {
            std::swap(_handle, other._handle);
            return *this;
        }

        SectionPin(SectionPin const&) = delete;
        SectionPin& operator=(SectionPin const&) = delete;

        /** Close the section handle, if any. */
        ~SectionPin();

        /** \return Whether the pin keeps a section alive. */
        explicit operator bool() const noexcept
        {
            return _handle != nullptr;
        }

    private:
        /** The section handle, or null. */
        void* _handle = nullptr;
    };

    class MXL_EXPORT SharedMemoryBase
    {
    public:
        /**
         * Return whether or not this instance currently represents a valid
         * mapping.
         */
        constexpr bool isValid() const noexcept;

        /**
         * Return whether or not this instance currently represents a valid
         * mapping.
         * \see isValid()
         */
        constexpr explicit operator bool() const noexcept;

        /** Return the size of the memory mapping in bytes. */
        constexpr std::size_t mappedSize() const noexcept;

        /**
         * Return the (normalized) access mode the segment is operating in.
         * \note Please note that the returned value is undefined for segments
         *      that don't currently represent a valid mapping.
         */
        constexpr AccessMode accessMode() const noexcept;

        /**
         * Return whether or the underlying file was created by this instance.
         */
        constexpr bool created() const noexcept;

        /**
         * Update the attributes of the file
         */
        void touch();

        /**
         * Duplicate the handle of the in-memory section of this segment, so that the data survives the segment object.
         * \return The pin, which is empty on platforms where the data lives in the file.
         */
        [[nodiscard]]
        SectionPin pinSection() const;

#ifdef _WIN32
        /**
         * \return The name of the in-memory section that holds the data of this segment. Other kernel objects that belong to the segment,
         *     like the wait queue of a flow, are named after it.
         */
        [[nodiscard]]
        std::wstring sectionName() const;
#endif

        constexpr void swap(SharedMemoryBase& other) noexcept;

    protected:
        constexpr SharedMemoryBase() noexcept;
        constexpr SharedMemoryBase(SharedMemoryBase&& other) noexcept;
        SharedMemoryBase(SharedMemoryBase const& other) = delete;

        /**
         * Create a shared memory mapping with the specified payload size, or
         * alterntively open it for reading and writing
         *
         * \param path The memory mapping path
         * \param mode The memory mapping access mode
         * \param payloadSize The minimum expected size of the shared memory
         * \throw If opening or creating the shared memory segment fails.
         */
        SharedMemoryBase(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode);

        /** Destructor. */
        ~SharedMemoryBase();

    protected:
        /**
         * Accessor for the mapped data
         * \return the mapped data, or the null pointer if this instance does
         *         not currently represent a mapped segment.
         */
        constexpr void* data() noexcept;

        /**
         * Accessor for the mapped data
         * \return the mapped data, or the null pointer if this instance does
         *         not currently represent a mapped segment.
         */
        constexpr void const* data() const noexcept;

        /**
         * Accessor for the mapped data
         * \return the mapped data, or the null pointer if this instance does
         *         not currently represent a mapped segment.
         */
        constexpr void const* cdata() const noexcept;

        /**
         * Check if the advisory lock for the shared memory file mapped by this objects
         * is exclusive or shared.
         * \throws If there is no lock at all, because either this is a read-only mapping of the memory, or because the object is in an invalid state.
         */
        [[nodiscard]]
        constexpr bool isExclusive() const;

        /**
         * Upgrade the lock held by the process for the shared memory file that is mapped by this object to an exclusive lock.
         * \return Returns true if the upgrade was successful. Returns false if another process also holds a shared lock and acquiring an exclusive
         * lock would be a blocking operation.
         * \throw std::runtime_error If this is a read-only mapping of the memory or if the object is in an invalid state.
         */
        bool makeExclusive();

    private:
        enum class LockType
        {
            None,
            Exclusive,
            Shared
        };

    private:
        /** File descriptor of the shared memory object. */
        int _fd;
        /**
         * A derivation of the mode used to open the segemnt that is used to
         * indicate the access mode the segement is operating in, as well as
         * whether or not it was created when it was last opened.
         */
        AccessMode _mode;
        /** Pointer to the mapped region. */
        void* _data;
        /** The size of the mapped region in bytes. */
        std::size_t _mappedSize;
        /** The type of advisory lock that is held for the mapped file */
        LockType _lockType;
        /** Windows: handle of the named in-memory section that holds the data. */
        void* _section;
    };

    struct SharedMemorySegment : SharedMemoryBase
    {
        constexpr SharedMemorySegment() noexcept;
        constexpr SharedMemorySegment(SharedMemorySegment&& other) noexcept;

        SharedMemorySegment(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode);

        using SharedMemoryBase::cdata;
        using SharedMemoryBase::data;

        SharedMemorySegment& operator=(SharedMemorySegment other) noexcept;
        constexpr void swap(SharedMemorySegment& other) noexcept;
    };

    /** ADL compatible shwap function for SharedMemorySegment. */
    constexpr void swap(SharedMemorySegment& lhs, SharedMemorySegment& rhs) noexcept;

    /**
     * Utility class that allocates and places a structure in shared memory
     */
    template<typename T>
    struct SharedMemoryInstance : SharedMemoryBase
    {
        constexpr SharedMemoryInstance() noexcept;
        constexpr SharedMemoryInstance(SharedMemoryInstance&& other);
        SharedMemoryInstance(SharedMemoryInstance const& other) = delete;

        /**
         * Create a shared memory mapping with the specified payload size, or
         * alterntively open it for reading and writing
         *
         * \param path The memory mapping path
         * \param mode The memory mapping access mode
         * \param extraSize Add this size to the shared memory in addition to sizeof(T)
         * \return true on success. false on conflicts or failure to create resources on disk.
         */
        SharedMemoryInstance(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode);

        SharedMemoryInstance& operator=(SharedMemoryInstance other) noexcept;

        constexpr void swap(SharedMemoryInstance& other) noexcept;

        /**
         * Accessor for the mapped data
         * \return the mapped data, or the null pointer if this instance does
         *         not currently represent a mapped segment.
         */
        constexpr T* get() noexcept;

        /**
         * Accessor for the mapped data
         * \return the mapped data, or the null pointer if this instance does
         *         not currently represent a mapped segment.
         */
        constexpr T const* get() const noexcept;

        using SharedMemoryBase::isExclusive;
        using SharedMemoryBase::makeExclusive;
    };

    /** ADL compatible shwap function for SharedMemorySegment. */
    template<typename T>
    constexpr void swap(SharedMemoryInstance<T>& lhs, SharedMemoryInstance<T>& rhs) noexcept;

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    constexpr SharedMemoryBase::SharedMemoryBase() noexcept
        : _fd{-1}
        , _mode{AccessMode::READ_ONLY}
        , _data{nullptr}
        , _mappedSize{0}
        , _lockType(LockType::None)
        , _section{nullptr}
    {}

    constexpr SharedMemoryBase::SharedMemoryBase(SharedMemoryBase&& other) noexcept
        : SharedMemoryBase{}
    {
        swap(other);
    }

    constexpr bool SharedMemoryBase::isValid() const noexcept
    {
        return (_data != nullptr);
    }

    constexpr SharedMemoryBase::operator bool() const noexcept
    {
        return isValid();
    }

    constexpr std::size_t SharedMemoryBase::mappedSize() const noexcept
    {
        return _mappedSize;
    }

    constexpr AccessMode SharedMemoryBase::accessMode() const noexcept
    {
        return (_mode == AccessMode::READ_ONLY) ? AccessMode::READ_ONLY : AccessMode::READ_WRITE;
    }

    constexpr bool SharedMemoryBase::created() const noexcept
    {
        return (_mode == AccessMode::CREATE_READ_WRITE);
    }

    constexpr void SharedMemoryBase::swap(SharedMemoryBase& other) noexcept
    {
        // Workaround for std::swap not being declared constexpr in libstdc++ v10
        constexpr auto const cx_swap = [](auto& lhs, auto& rhs) constexpr noexcept
        {
            auto temp = lhs;
            lhs = rhs;
            rhs = temp;
        };

        cx_swap(_fd, other._fd);
        cx_swap(_mode, other._mode);
        cx_swap(_data, other._data);
        cx_swap(_mappedSize, other._mappedSize);
        cx_swap(_lockType, other._lockType);
        cx_swap(_section, other._section);
    }

    constexpr void* SharedMemoryBase::data() noexcept
    {
        return _data;
    }

    constexpr void const* SharedMemoryBase::data() const noexcept
    {
        return _data;
    }

    constexpr void const* SharedMemoryBase::cdata() const noexcept
    {
        return _data;
    }

    constexpr bool SharedMemoryBase::isExclusive() const
    {
        return _lockType == LockType::Exclusive;
    }

    constexpr SharedMemorySegment::SharedMemorySegment() noexcept
        : SharedMemoryBase{}
    {}

    constexpr SharedMemorySegment::SharedMemorySegment(SharedMemorySegment&& other) noexcept
        : SharedMemoryBase{std::move(other)}
    {}

    inline SharedMemorySegment::SharedMemorySegment(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode)
        : SharedMemoryBase{path, mode, payloadSize, lockMode}
    {}

    inline SharedMemorySegment& SharedMemorySegment::operator=(SharedMemorySegment other) noexcept
    {
        swap(other);
        return *this;
    }

    constexpr void SharedMemorySegment::swap(SharedMemorySegment& other) noexcept
    {
        static_cast<SharedMemoryBase*>(this)->swap(other);
    }

    constexpr void swap(SharedMemorySegment& lhs, SharedMemorySegment& rhs) noexcept
    {
        lhs.swap(rhs);
    }

    template<typename T>
    constexpr SharedMemoryInstance<T>::SharedMemoryInstance() noexcept
        : SharedMemoryBase{}
    {}

    template<typename T>
    constexpr SharedMemoryInstance<T>::SharedMemoryInstance(SharedMemoryInstance&& other)
        : SharedMemoryBase{std::move(other)}
    {}

    template<typename T>
    inline SharedMemoryInstance<T>::SharedMemoryInstance(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode)
        : SharedMemoryBase{path, mode, payloadSize + sizeof(T), lockMode}
    {
        if (created())
        {
            if (this->mappedSize() < (payloadSize + sizeof(T)))
            {
                throw std::runtime_error("Cannot initialize shared memory instance because not enough memory was mapped.");
            }

            new (data()) T{};
        }
    }

    template<typename T>
    inline auto SharedMemoryInstance<T>::operator=(SharedMemoryInstance other) noexcept -> SharedMemoryInstance&
    {
        swap(other);
        return *this;
    }

    template<typename T>
    constexpr void SharedMemoryInstance<T>::swap(SharedMemoryInstance& other) noexcept
    {
        static_cast<SharedMemoryBase*>(this)->swap(other);
    }

    template<typename T>
    constexpr T* SharedMemoryInstance<T>::get() noexcept
    {
        return static_cast<T*>(data());
        ;
    }

    template<typename T>
    constexpr T const* SharedMemoryInstance<T>::get() const noexcept
    {
        return static_cast<T const*>(data());
    }

    template<typename T>
    constexpr void swap(SharedMemoryInstance<T>& lhs, SharedMemoryInstance<T>& rhs) noexcept
    {
        lhs.swap(rhs);
    }
}
