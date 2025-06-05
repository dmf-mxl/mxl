#pragma once

#include <cstddef>
#include <utility>

namespace mxl::lib
{
    enum class AccessMode
    {
        READ_ONLY,
        READ_WRITE,
        CREATE_READ_WRITE
    };

    class SharedMemoryBase
    {
    public:
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
        SharedMemoryBase(char const* path, AccessMode mode, std::size_t payloadSize);

        /** Destructor. */
        ~SharedMemoryBase();

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
    };

    struct SharedMemorySegment
        : SharedMemoryBase
    {
        using SharedMemoryBase::SharedMemoryBase;

        using SharedMemoryBase::data;
        using SharedMemoryBase::cdata;

        SharedMemorySegment& operator=(SharedMemorySegment other) noexcept;
        constexpr void swap(SharedMemorySegment& other) noexcept;
    };

    /** ADL compatible shwap function for SharedMemorySegment. */
    constexpr void swap(SharedMemorySegment& lhs, SharedMemorySegment& rhs) noexcept;


    /**
     * Utility class that allocates and places a structure in shared memory
     */
    template<typename T>
    struct SharedMemoryInstance
        : SharedMemoryBase
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
        SharedMemoryInstance(char const* path, AccessMode mode, std::size_t payloadSize);

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
        return (_mode == AccessMode::READ_ONLY)
            ? AccessMode::READ_ONLY
            : AccessMode::READ_WRITE;
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
    inline SharedMemoryInstance<T>::SharedMemoryInstance(char const* path, AccessMode mode, std::size_t payloadSize)
        : SharedMemoryBase{path, mode, payloadSize + sizeof(T)}
    {
        if (created())
        {
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
        return static_cast<T*>(data());;
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
