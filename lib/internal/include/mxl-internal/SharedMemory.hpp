// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <new>
#include <stdexcept>
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
