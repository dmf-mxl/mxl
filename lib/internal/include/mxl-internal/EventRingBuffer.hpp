// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Atomic single-producer event ring storage and snapshot protocol.
 */

#pragma once

#include <cstring>
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include "Flow.hpp"

namespace mxl::lib
{
    /** @brief Immutable 4 KiB geometry header at the start of the single event ring file. */
    struct EventRingHeader
    {
        std::uint32_t version;            ///< Storage version checked when the ring is opened.
        std::uint32_t size;               ///< Size of this header in bytes.
        std::uint32_t eventCount;         ///< Number of physical slots; every slot may retain an entry.
        std::uint32_t payloadSize;        ///< Maximum payload bytes stored in one slot.
        std::uint8_t reserved[4096 - 16]; ///< Zero-initialized padding to 4096 bytes.
    };

    static_assert(sizeof(EventRingHeader) == 4096);
    static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic_ref<std::uint64_t>::required_alignment <= alignof(std::uint64_t));

    /**
     * @brief Non-owning single-producer event ring view safe for concurrent snapshot readers.
     *
     * Metadata and payload are copied through atomic words; sequence tags detect
     * overwrite during a read. The writer owns head advancement, timestamp validation
     * and notifications. No shared tail or writer reservation cursor is stored here.
     * The mapping must outlive this view and all calls through it.
     *
     * Inspired by vt-tv/lockfree_ipc_ringbuffer revision
     * ffd97a3cb0162109920af04408b54c2703aa9e4e; see docs/event-flows.md.
     */
    class EventRingBuffer
    {
    public:
        constexpr static auto STORAGE_VERSION = std::uint32_t{1};                ///< Version used by immutable ring and slot headers.
        constexpr static auto EMPTY = std::numeric_limits<std::uint64_t>::max(); ///< Tag identifying a slot with no publication.
        constexpr static auto MAX_INDEX = (EMPTY >> 1) - 1;                      ///< Largest index whose complete tag does not collide with EMPTY.

        /** @brief Outcome of a bounded attempt to copy an entry without waiting. */
        enum class ReadResult
        {
            Ready,       ///< Metadata and payload form a validated snapshot of the requested entry.
            Pending,     ///< The requested entry is not yet published or its index is unrepresentable.
            Overwritten, ///< The slot advanced past this index or changed while being copied.
            Invalid      ///< A stable snapshot contains unsupported or invalid metadata.
        };

        /**
         * @brief Compute the physical stride between adjacent slots.
         * @param payloadSize Maximum payload bytes per entry.
         * @return Slot header plus payload capacity, rounded up to the 64-byte Event alignment.
         */
        static std::size_t slotStride(std::uint32_t payloadSize) noexcept
        {
            constexpr auto alignment = std::size_t{alignof(Event)};
            return (sizeof(Event) + payloadSize + alignment - 1) / alignment * alignment;
        }

        /**
         * @brief Validate geometry and compute the complete mapping length.
         * @param count Number of slots, from 2 through 65536.
         * @param payloadSize Maximum bytes per entry, from 1 through 1048576.
         * @return Ring header size plus the stride of every slot.
         * @throws std::invalid_argument Geometry is unsupported or its byte count overflows.
         */
        static std::size_t bufferSize(std::uint32_t count, std::uint32_t payloadSize)
        {
            if (count < 2 || count > 65536 || payloadSize == 0 || payloadSize > 1048576 ||
                slotStride(payloadSize) > (std::numeric_limits<std::size_t>::max() - sizeof(EventRingHeader)) / count)
            {
                throw std::invalid_argument{"Invalid event buffer dimensions."};
            }
            return sizeof(EventRingHeader) + (slotStride(payloadSize) * count);
        }

        /**
         * @brief Construct headers, empty sequence tags and payload words in a fresh mapping.
         * @param[out] memory Writable storage of at least bufferSize(count, payloadSize) bytes.
         * @param count Number of slots to construct.
         * @param payloadSize Maximum bytes per entry.
         * @pre memory is aligned for Event, and no reader or writer accesses it yet.
         * @throws std::invalid_argument Geometry is invalid.
         */
        static void initialize(void* memory, std::uint32_t count, std::uint32_t payloadSize)
        {
            (void)bufferSize(count, payloadSize);
            auto header = new (memory) EventRingHeader{};
            header->version = STORAGE_VERSION;
            header->size = sizeof(EventRingHeader);
            header->eventCount = count;
            header->payloadSize = payloadSize;
            for (auto i = std::size_t{0}; i < count; ++i)
            {
                auto bytes = static_cast<std::uint8_t*>(memory) + sizeof(EventRingHeader) + (i * slotStride(payloadSize));
                auto event = new (bytes) Event{};
                event->header.version = STORAGE_VERSION;
                event->header.size = sizeof(EventHeader);
                event->header.sequence = EMPTY;
                new (bytes + sizeof(Event)) std::uint64_t[(payloadSize + 7) / 8]{};
            }
        }

        /**
         * @brief Attach a view and validate immutable headers across all slots.
         * @param memory Initialized mapping, possibly read-only for a reader.
         * @param count Expected slot count from the common flow configuration.
         * @param payloadSize Expected per-entry payload capacity.
         * @pre memory is correctly aligned and covers bufferSize(count, payloadSize) bytes.
         * @throws std::invalid_argument Geometry or a ring/slot header is invalid.
         */
        EventRingBuffer(void* memory, std::uint32_t count, std::uint32_t payloadSize)
            : _header{static_cast<EventRingHeader*>(memory)}
            , _count{count}
            , _payloadSize{payloadSize}
            , _stride{slotStride(payloadSize)}
        {
            (void)bufferSize(count, payloadSize);
            if (_header->version != STORAGE_VERSION || _header->size != sizeof(EventRingHeader) || _header->eventCount != count ||
                _header->payloadSize != payloadSize)
            {
                throw std::invalid_argument{"Invalid event ring header."};
            }
            // Only immutable fields may be inspected while another process publishes.
            for (auto i = std::size_t{0}; i < count; ++i)
            {
                auto const& header = slot(i).header;
                if (header.version != STORAGE_VERSION || header.size != sizeof(EventHeader))
                {
                    throw std::invalid_argument{"Invalid event slot header."};
                }
            }
        }

        /**
         * @brief Atomically publish metadata and payload under a new sequence tag.
         * @param index Queue index selected by the sole producer, normally headIndex + 1.
         * @param info Validated entry metadata; eventSize must not exceed the ring capacity.
         * @param payload Source of at least info.eventSize bytes, stable for the call.
         * @pre The caller exclusively owns publication and the mapping is writable.
         * @retval MXL_STATUS_OK The entry is published; the caller may advance the head.
         * @retval MXL_ERR_OUT_OF_RANGE_TOO_LATE index exceeds MAX_INDEX.
         * @retval MXL_ERR_FLOW_INVALID The slot already holds this or a newer sequence tag.
         * @note This primitive does not validate timestamps or update the shared head.
         */
        mxlStatus publish(std::uint64_t index, mxlEventInfo const& info, std::uint8_t const* payload) noexcept
        {
            if (index > MAX_INDEX)
            {
                return MXL_ERR_OUT_OF_RANGE_TOO_LATE;
            }
            auto& event = slot(index);
            auto sequence = std::atomic_ref{event.header.sequence};
            auto const previous = sequence.load(std::memory_order_relaxed);
            if (previous != EMPTY && (previous >> 1) >= index)
            {
                // A previous producer may have died before updating headIndex.
                // Reusing its tag could make readers accept an inconsistent copy.
                return MXL_ERR_FLOW_INVALID;
            }
            sequence.store(index << 1, std::memory_order_release);
            storeBytes(event.header.infoWords, &info, sizeof info);
            storeBytes(payloadWords(event), payload, info.eventSize);
            sequence.store((index << 1) | 1, std::memory_order_release);
            return MXL_STATUS_OK;
        }

        /**
         * @brief Copy one entry with sequence validation before and after the copy.
         * @param index Queue index to read.
         * @param[out] info Destination for metadata.
         * @param[out] payload Private destination with at least the configured payload capacity.
         * @return Snapshot outcome; outputs are usable only when the result is ReadResult::Ready.
         * @pre Concurrent calls use separate output storage. The mapping remains alive.
         */
        ReadResult read(std::uint64_t index, mxlEventInfo& info, std::uint8_t* payload) const noexcept
        {
            if (index > MAX_INDEX)
            {
                return ReadResult::Pending;
            }
            auto const& event = slot(index);
            auto const expected = (index << 1) | 1;
            auto const before = load(event.header.sequence);
            if (before != expected)
            {
                return before != EMPTY && (before >> 1) > index ? ReadResult::Overwritten : ReadResult::Pending;
            }
            loadBytes(&info, event.header.infoWords, sizeof info);
            // Metadata may be an inconsistent snapshot until the final sequence check.
            if (info.eventSize <= _payloadSize)
            {
                loadBytes(payload, payloadWords(event), info.eventSize);
            }
            if (load(event.header.sequence) != before)
            {
                return ReadResult::Overwritten;
            }
            if (info.version != EVENT_HEADER_VERSION || info.size != sizeof info || info.flags != 0 || info.eventSize > _payloadSize)
            {
                return ReadResult::Invalid;
            }
            return ReadResult::Ready;
        }

        /**
         * @brief Load a shared 64-bit word with acquire ordering, including from a read-only mapping.
         * @param value Naturally aligned word accessed atomically by concurrent users.
         * @return The atomically observed value.
         */
        static std::uint64_t load(std::uint64_t const& value) noexcept
        {
            // libc++ versions used by MXL do not support atomic_ref<const T>.
            // This performs loads only, including on read-only IPC mappings.
            return std::atomic_ref{const_cast<std::uint64_t&>(value)}.load(std::memory_order_acquire);
        }

    private:
        /**
         * @brief Resolve a logical queue index to a physical slot modulo capacity.
         * @param index Queue index whose slot is needed.
         * @return Slot header in the borrowed mapping; access must obey the atomic protocol.
         */
        Event& slot(std::uint64_t index) const noexcept
        {
            return *reinterpret_cast<Event*>(reinterpret_cast<std::uint8_t*>(_header) + sizeof(EventRingHeader) + ((index % _count) * _stride));
        }

        /**
         * @brief Locate the atomic payload words immediately after an event slot header.
         * @param event Slot header inside a live mapping.
         * @return Payload word address; callers respect mapping permissions and use atomic access.
         */
        static std::uint64_t* payloadWords(Event const& event) noexcept
        {
            return reinterpret_cast<std::uint64_t*>(reinterpret_cast<std::uint8_t*>(const_cast<Event*>(&event)) + sizeof(Event));
        }

        /**
         * @brief Copy private bytes to shared words using release stores.
         * @param[out] destination Aligned words with space for size rounded up to eight bytes.
         * @param source Stable private bytes to publish.
         * @param size Byte count; zero permits a null source.
         */
        static void storeBytes(std::uint64_t* destination, void const* source, std::size_t size) noexcept
        {
            auto const bytes = static_cast<std::uint8_t const*>(source);
            for (auto offset = std::size_t{0}; offset < size; offset += sizeof(std::uint64_t))
            {
                auto word = std::uint64_t{};
                std::memcpy(&word, bytes + offset, std::min(sizeof word, size - offset));
                // If a reader observes any new word, this release/acquire pair
                // forces its final sequence load to observe the writing tag or later.
                std::atomic_ref{destination[offset / sizeof word]}.store(word, std::memory_order_release);
            }
        }

        /**
         * @brief Copy shared words into private bytes using acquire loads.
         * @param[out] destination Private storage covering size bytes.
         * @param source Aligned shared words covering size rounded up to eight bytes.
         * @param size Byte count to copy; the final word may be copied partially.
         */
        static void loadBytes(void* destination, std::uint64_t const* source, std::size_t size) noexcept
        {
            auto bytes = static_cast<std::uint8_t*>(destination);
            for (auto offset = std::size_t{0}; offset < size; offset += sizeof(std::uint64_t))
            {
                auto const word = load(source[offset / sizeof(std::uint64_t)]);
                std::memcpy(bytes + offset, &word, std::min(sizeof word, size - offset));
            }
        }

        EventRingHeader* _header;   ///< Borrowed address of the mapping's immutable ring header.
        std::uint32_t _count;       ///< Validated physical slot count.
        std::uint32_t _payloadSize; ///< Validated per-entry payload capacity in bytes.
        std::size_t _stride;        ///< Aligned distance in bytes between slot headers.
    };
}
