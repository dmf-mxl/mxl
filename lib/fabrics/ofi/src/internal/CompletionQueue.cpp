// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file CompletionQueue.cpp
 * @brief Implementation of CompletionQueue wrapper for libfabric completion queues
 */

#include "CompletionQueue.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <sys/types.h>
#include <mxl-internal/Logging.hpp>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include "Completion.hpp"
#include "Domain.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    // Return default attributes: queue size=8, wait object unspecified
    CompletionQueue::Attributes CompletionQueue::Attributes::defaults()
    {
        CompletionQueue::Attributes attr{};
        attr.size = 8;                    // Queue depth - can hold 8 completions before full
        attr.waitObject = FI_WAIT_UNSPEC; // Let provider choose wait mechanism (note: EFA may require FI_WAIT_NONE)
        return attr;
    }

    // Convert our Attributes to libfabric's fi_cq_attr structure
    ::fi_cq_attr CompletionQueue::Attributes::raw() const noexcept
    {
        ::fi_cq_attr raw{};
        raw.size = size;                     // Queue depth
        raw.wait_obj = waitObject;           // Wait mechanism (FI_WAIT_UNSPEC, FI_WAIT_FD, FI_WAIT_NONE, etc.)
        raw.format = FI_CQ_FORMAT_DATA;      // Use DATA format (includes flags, data, op_context)
        raw.wait_cond = FI_CQ_COND_NONE;     // No special wait conditions
        raw.wait_set = nullptr;              // Only used if wait_obj is FI_WAIT_SET
        raw.flags = 0;                       // Could use FI_AFFINITY to pin interrupts to a CPU core
        raw.signaling_vector = 0;            // CPU core for interrupt affinity (if FI_AFFINITY set)
        return raw;
    }

    // Factory method: create and open a completion queue
    std::shared_ptr<CompletionQueue> CompletionQueue::open(std::shared_ptr<Domain> domain, CompletionQueue::Attributes const& attr)
    {
        ::fid_cq* cq;
        auto cq_attr = attr.raw();

        // Call libfabric to open the completion queue
        fiCall(::fi_cq_open, "Failed to open completion queue", domain->raw(), &cq_attr, &cq, nullptr);

        // Work around private constructor for std::make_shared
        struct MakeSharedEnabler : public CompletionQueue
        {
            MakeSharedEnabler(::fid_cq* raw, std::shared_ptr<Domain> domain)
                : CompletionQueue(raw, domain)
            {}
        };

        return std::make_shared<MakeSharedEnabler>(cq, domain);
    }

    // Non-blocking read: poll the CQ once and return immediately
    std::optional<Completion> CompletionQueue::read()
    {
        fi_cq_data_entry entry;

        // fi_cq_read: non-blocking read of 1 completion
        // Returns: number of completions read (1), or negative error code
        ssize_t ret = fi_cq_read(_raw, &entry, 1);

        return handleReadResult(ret, entry);
    }

    // Blocking read: wait up to timeout for a completion to arrive
    std::optional<Completion> CompletionQueue::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        // Convert duration to milliseconds for fi_cq_sread
        auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
        if (timeoutMs == 0)
        {
            // Zero timeout means don't block - just do a regular poll
            return read();
        }

        fi_cq_data_entry entry;

        // fi_cq_sread: blocking read with timeout (s = synchronous)
        // Blocks until: completion arrives, timeout expires, or signal interrupts
        ssize_t ret = fi_cq_sread(_raw, &entry, 1, nullptr, timeoutMs);
        return handleReadResult(ret, entry);
    }

    // Private constructor - initialize from raw fi_cq pointer
    CompletionQueue::CompletionQueue(::fid_cq* raw, std::shared_ptr<Domain> domain)
        : _raw(raw)
        , _domain(std::move(domain))  // Hold onto domain to keep it alive
    {}

    // Destructor - close the CQ and release resources
    CompletionQueue::~CompletionQueue()
    {
        close();
    }

    // Accessor for mutable raw pointer
    ::fid_cq* CompletionQueue::raw() noexcept
    {
        return _raw;
    }

    // Accessor for const raw pointer
    ::fid_cq const* CompletionQueue::raw() const noexcept
    {
        return _raw;
    }

    // Internal helper to close the CQ
    void CompletionQueue::close()
    {
        if (_raw)
        {
            MXL_DEBUG("Closing completion queue");

            fiCall(::fi_close, "Failed to close completion queue", &_raw->fid);
            _raw = nullptr;
        }
    }

    // Common handler for both read() and readBlocking() - interprets the return code
    std::optional<Completion> CompletionQueue::handleReadResult(ssize_t ret, ::fi_cq_data_entry const& entry)
    {
        if (ret == -FI_EAGAIN)
        {
            // No completion available right now (would block)
            return std::nullopt;
        }

        if (ret == -FI_EAVAIL)
        {
            // A completion is available, but it's an ERROR completion
            // Error completions go into a separate error queue that we must read with fi_cq_readerr
            ::fi_cq_err_entry err;
            fi_cq_readerr(_raw, &err, 0);

            // Wrap the error in a Completion::Error and return it
            // Pass shared_from_this() so the error can call fi_cq_strerror later
            return Completion{
                Completion::Error{err, this->shared_from_this()}
            };
        }

        if (ret < 0)
        {
            // Some other error occurred (not EAGAIN, not EAVAIL)
            throw FabricException::make(ret, "Failed to read completion from queue: {}", ::fi_strerror(ret));
        }

        // Success! ret > 0 means we got a completion
        // Wrap the raw entry in a Completion::Data and return it
        return Completion{Completion::Data{entry}};
    }
}
