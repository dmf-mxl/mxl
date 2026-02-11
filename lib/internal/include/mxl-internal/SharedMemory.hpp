// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file SharedMemory.hpp
 * @brief Zero-copy shared memory mapping with advisory locking
 *
 * This is the foundation of MXL's zero-copy architecture. All flow state, grain data,
 * and sample buffers live in memory-mapped files (typically on tmpfs).
 *
 * KEY ARCHITECTURAL DECISIONS:
 *
 * 1. Memory-mapped files vs. POSIX shm_open:
 *    - We use regular files (on tmpfs) instead of shm_open
 *    - Allows hierarchical organization (domain/flow.mxl-flow/grains/data.0)
 *    - Easier garbage collection (just remove stale files)
 *    - Works with existing filesystem permissions/quotas
 *
 * 2. Advisory file locking (fcntl):
 *    - FlowWriters hold shared or exclusive locks on the "data" file
 *    - Used for garbage collection: can only delete flows with no active locks
 *    - NOT used for synchronization (we use futexes for that)
 *    - Locks are per-file-descriptor, released when fd closes
 *
 * 3. Read-only vs. read-write mappings:
 *    - FlowReaders typically use PROT_READ (can't accidentally corrupt data)
 *    - FlowWriters use PROT_READ|PROT_WRITE
 *    - Readers can still use futexes on PROT_READ memory (futex doesn't need write access)
 *
 * 4. Lazy allocation:
 *    - Files are created with ftruncate to desired size
 *    - Physical pages allocated on first write (sparse files)
 *    - Keeps memory usage low for inactive flows
 *
 * 5. Lock upgrade capability:
 *    - SharedMemoryBase::makeExclusive() upgrades shared lock to exclusive
 *    - Used when FlowWriter needs exclusive access to update flow state
 *    - Returns false if another writer holds shared lock (non-blocking)
 */

#pragma once

#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>
#include <mxl-internal/Logging.hpp>
#include <mxl/platform.h>

namespace mxl::lib
{
    /**
     * Advisory lock mode for file-based locks (fcntl).
     * Used for garbage collection coordination, NOT for data synchronization.
     */
    enum class LockMode
    {
        Exclusive,  // Only one process can hold exclusive lock (typical for writers)
        Shared,     // Multiple processes can hold shared locks (typical for readers)
        None,       // No advisory lock held (read-only access, no GC protection)
    };

    /**
     * Access mode for shared memory mapping.
     * Determines both mmap() protection and whether to create/open the file.
     */
    enum class AccessMode
    {
        READ_ONLY,         // Open existing file with PROT_READ (for readers)
        READ_WRITE,        // Open existing file with PROT_READ|PROT_WRITE (for writers)
        CREATE_READ_WRITE  // Create new file with PROT_READ|PROT_WRITE (for writers creating flows)
    };

    /**
     * Base class for shared memory mappings.
     *
     * Manages:
     * - File descriptor for the memory-mapped file
     * - mmap() pointer and size
     * - Advisory file lock (for garbage collection coordination)
     * - Access mode tracking
     *
     * Thread-safety:
     * - Not thread-safe (each thread should have its own SharedMemory instance)
     * - Multiple processes/threads can map the same file concurrently
     * - Advisory locks are process-wide (all threads share the lock state)
     *
     * Lifecycle:
     * - Constructor opens/creates file, establishes mmap, acquires lock
     * - Destructor releases lock, unmaps memory, closes file descriptor
     * - Move semantics supported (ownership transfer)
     * - Copy semantics deleted (can't have two owners of same fd/lock)
     */
    class MXL_EXPORT SharedMemoryBase
    {
    public:
        /**
         * Check if this instance represents a valid memory mapping.
         * A mapping is valid if _data is non-null (successfully mapped).
         *
         * @return true if mapped, false if uninitialized or mapping failed
         */
        constexpr bool isValid() const noexcept;

        /**
         * Bool conversion operator: allows "if (sharedMem) { ... }" idiom.
         * Equivalent to isValid().
         */
        constexpr explicit operator bool() const noexcept;

        /**
         * Get the size of the mapped region in bytes.
         * This is the size passed to mmap(), which may be larger than the
         * requested payload size due to page alignment.
         *
         * @return Mapped region size in bytes, or 0 if not mapped
         */
        constexpr std::size_t mappedSize() const noexcept;

        /**
         * Get the access mode of this mapping.
         * CREATE_READ_WRITE is normalized to READ_WRITE after construction
         * (the "created" flag tracks whether we created it).
         *
         * @return READ_ONLY or READ_WRITE (never CREATE_READ_WRITE after construction)
         */
        constexpr AccessMode accessMode() const noexcept;

        /**
         * Check if this instance created the underlying file.
         * True if opened with CREATE_READ_WRITE and file didn't exist.
         * Used to determine whether to initialize shared memory structures.
         *
         * @return true if this instance created the file, false if opened existing
         */
        constexpr bool created() const noexcept;

        /**
         * Update the file's access/modification timestamps (touch).
         * Used by FlowWriter to indicate the flow is still active.
         * Helps garbage collection identify stale flows.
         */
        void touch();

        /** Swap contents with another SharedMemoryBase (used for move assignment). */
        constexpr void swap(SharedMemoryBase& other) noexcept;

    protected:
        /** Default constructor: creates invalid mapping (for deferred initialization). */
        constexpr SharedMemoryBase() noexcept;

        /** Move constructor: transfers ownership of mapping and lock. */
        constexpr SharedMemoryBase(SharedMemoryBase&& other) noexcept;

        /** Copy constructor deleted: can't have two owners of same file descriptor. */
        SharedMemoryBase(SharedMemoryBase const& other) = delete;

        /**
         * Create or open a shared memory mapping.
         *
         * Steps:
         * 1. Open file with O_RDONLY or O_RDWR depending on mode
         *    - CREATE_READ_WRITE uses O_CREAT|O_EXCL (fail if exists)
         * 2. Acquire advisory lock (fcntl F_SETLK)
         *    - Shared lock for readers (LockMode::Shared)
         *    - Exclusive lock for writers (LockMode::Exclusive)
         *    - No lock for LockMode::None
         * 3. Resize file to payloadSize using ftruncate (if creating)
         * 4. mmap() with PROT_READ or PROT_READ|PROT_WRITE
         *    - Uses MAP_SHARED for cross-process visibility
         *    - Pages allocated lazily on first write
         *
         * @param path Filesystem path to shared memory file
         * @param mode READ_ONLY, READ_WRITE, or CREATE_READ_WRITE
         * @param payloadSize Minimum size in bytes (file truncated to this size if creating)
         * @param lockMode Advisory lock mode for garbage collection coordination
         * @throws std::runtime_error if open/mmap/lock fails
         */
        SharedMemoryBase(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode);

        /**
         * Destructor: unmaps memory, releases advisory lock, closes file descriptor.
         * Order matters:
         * 1. munmap() - release virtual address space
         * 2. Lock automatically released when fd closed (no explicit unlock needed)
         * 3. close() - release file descriptor
         */
        ~SharedMemoryBase();

    protected:
        /**
         * Get mutable pointer to mapped memory (for writers).
         * This is protected because external code should use SharedMemorySegment
         * or SharedMemoryInstance which expose type-safe accessors.
         *
         * @return Pointer to mapped memory, or nullptr if not mapped
         */
        constexpr void* data() noexcept;

        /**
         * Get const pointer to mapped memory (for readers).
         *
         * @return Const pointer to mapped memory, or nullptr if not mapped
         */
        constexpr void const* data() const noexcept;

        /**
         * Get const pointer to mapped memory (explicit const version).
         * Equivalent to data() const, provided for API consistency.
         *
         * @return Const pointer to mapped memory, or nullptr if not mapped
         */
        constexpr void const* cdata() const noexcept;

        /**
         * Check if the advisory lock is exclusive (vs. shared).
         * Exclusive locks allow only one writer; shared locks allow multiple readers.
         *
         * @return true if lock is exclusive, false if shared
         * @throws std::logic_error if no lock is held (LockMode::None)
         *
         * Note: This queries _lockType, not the kernel. It reflects the lock
         * acquired during construction, not runtime lock state changes from other processes.
         */
        [[nodiscard]]
        constexpr bool isExclusive() const;

        /**
         * Upgrade advisory lock from shared to exclusive (non-blocking).
         *
         * Use case: FlowWriter initially acquires shared lock, then needs exclusive
         * access to update flow state. Attempts upgrade without blocking.
         *
         * Implementation: Uses fcntl(F_SETLK, F_WRLCK) - fails immediately if
         * another process holds shared lock.
         *
         * @return true if upgrade succeeded, false if would block (another lock holder exists)
         * @throws std::runtime_error if mapping is read-only or invalid
         *
         * Thread-safety: Advisory locks are per-process, not per-thread. Upgrading
         * affects all threads in this process.
         */
        bool makeExclusive();

    private:
        /**
         * Internal lock type tracking (separate from LockMode to distinguish None from uninitialized).
         * This tracks what lock was actually acquired, used for isExclusive() and makeExclusive().
         */
        enum class LockType
        {
            None,       // No advisory lock held
            Exclusive,  // Exclusive advisory lock (F_WRLCK)
            Shared      // Shared advisory lock (F_RDLCK)
        };

    private:
        /**
         * File descriptor of the shared memory object.
         * - Obtained from open() during construction
         * - Used for mmap(), fcntl() locking, ftruncate()
         * - Closed in destructor (which also releases advisory lock)
         * - Value is -1 for invalid/uninitialized instances
         */
        int _fd;

        /**
         * Access mode of this mapping.
         * - CREATE_READ_WRITE normalized to READ_WRITE after creation
         * - Used to determine mmap() protection flags
         * - Stored separately from mmap() so we can query mode after mapping
         */
        AccessMode _mode;

        /**
         * Pointer to the mapped memory region.
         * - Obtained from mmap() during construction
         * - Used by all data access (via data() accessors)
         * - Unmapped with munmap() in destructor
         * - Value is nullptr for invalid/uninitialized instances
         */
        void* _data;

        /**
         * Size of the mapped region in bytes.
         * - May be larger than requested payload due to page alignment
         * - Reflects actual mmap() size, not file size
         * - Used for munmap() in destructor
         */
        std::size_t _mappedSize;

        /**
         * Type of advisory lock currently held.
         * - Tracks state for isExclusive() and makeExclusive()
         * - Updated when lock is upgraded
         * - Note: This is our view of the lock; kernel is authoritative source
         */
        LockType _lockType;
    };

    /**
     * Shared memory segment with raw byte access.
     *
     * Use this when you need a generic memory region (e.g., grain data, audio samples).
     * For structured data (e.g., FlowState), use SharedMemoryInstance<T> instead.
     *
     * Exposes data() and cdata() publicly for direct memory access.
     */
    struct SharedMemorySegment : SharedMemoryBase
    {
        /** Default constructor: invalid segment. */
        constexpr SharedMemorySegment() noexcept;

        /** Move constructor: transfer ownership. */
        constexpr SharedMemorySegment(SharedMemorySegment&& other) noexcept;

        /**
         * Create or open a shared memory segment.
         * See SharedMemoryBase constructor for details.
         */
        SharedMemorySegment(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode);

        // Expose data accessors publicly (they're protected in base class)
        using SharedMemoryBase::cdata;
        using SharedMemoryBase::data;

        /** Move assignment (implemented via swap idiom). */
        SharedMemorySegment& operator=(SharedMemorySegment other) noexcept;

        /** Swap contents with another segment. */
        constexpr void swap(SharedMemorySegment& other) noexcept;
    };

    /** ADL-compatible swap function for SharedMemorySegment (enables std::swap). */
    constexpr void swap(SharedMemorySegment& lhs, SharedMemorySegment& rhs) noexcept;

    /**
     * Shared memory instance with typed structure access and placement-new construction.
     *
     * This template is used for structured shared memory objects like FlowState.
     * Key features:
     * - Automatically allocates sizeof(T) + extraSize bytes
     * - If creating (CREATE_READ_WRITE), uses placement-new to construct T in shared memory
     * - Provides type-safe get() accessor instead of raw void* pointer
     * - Exposes isExclusive() and makeExclusive() for lock management
     *
     * Typical usage:
     *   SharedMemoryInstance<FlowState> state("/path/data", CREATE_READ_WRITE, extraSize, Exclusive);
     *   if (state.created()) {
     *       // state->field was already initialized by placement-new
     *   }
     *   state.get()->grainCount++;  // Access fields
     *
     * @tparam T Type of structure to place in shared memory (must be trivially destructible)
     */
    template<typename T>
    struct SharedMemoryInstance : SharedMemoryBase
    {
        /** Default constructor: invalid instance. */
        constexpr SharedMemoryInstance() noexcept;

        /** Move constructor: transfer ownership. */
        constexpr SharedMemoryInstance(SharedMemoryInstance&& other);

        /** Copy constructor deleted: can't have two owners. */
        SharedMemoryInstance(SharedMemoryInstance const& other) = delete;

        /**
         * Create or open a shared memory instance.
         *
         * Allocates sizeof(T) + payloadSize bytes in shared memory.
         * If creating (mode == CREATE_READ_WRITE), uses placement-new to
         * default-construct T at the start of the mapped region.
         *
         * The extraSize parameter allows embedding variable-length data after T
         * (e.g., T might have a flexible array member at the end).
         *
         * @param path Filesystem path to shared memory file
         * @param mode READ_ONLY, READ_WRITE, or CREATE_READ_WRITE
         * @param payloadSize Extra bytes beyond sizeof(T)
         * @param lockMode Advisory lock mode
         * @throws std::runtime_error if creation/mapping fails or insufficient space
         */
        SharedMemoryInstance(char const* path, AccessMode mode, std::size_t payloadSize, LockMode lockMode);

        /** Move assignment (implemented via swap idiom). */
        SharedMemoryInstance& operator=(SharedMemoryInstance other) noexcept;

        /** Swap contents with another instance. */
        constexpr void swap(SharedMemoryInstance& other) noexcept;

        /**
         * Get mutable pointer to the structure in shared memory.
         * @return Pointer to T, or nullptr if not mapped
         */
        constexpr T* get() noexcept;

        /**
         * Get const pointer to the structure in shared memory.
         * @return Const pointer to T, or nullptr if not mapped
         */
        constexpr T const* get() const noexcept;

        // Expose lock management functions publicly
        using SharedMemoryBase::isExclusive;
        using SharedMemoryBase::makeExclusive;
    };

    /** ADL-compatible swap function for SharedMemoryInstance (enables std::swap). */
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
        // If we created the file, initialize the structure with placement-new
        if (created())
        {
            // Sanity check: ensure we actually got enough memory
            if (this->mappedSize() < (payloadSize + sizeof(T)))
            {
                throw std::runtime_error("Cannot initialize shared memory instance because not enough memory was mapped.");
            }

            // Placement-new: construct T in the mapped memory region
            // This calls T's default constructor, initializing all fields
            // T must be trivially destructible (we never call destructor)
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
