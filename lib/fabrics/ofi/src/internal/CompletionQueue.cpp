// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include "CompletionQueue.hpp"
#include <memory>
#include <optional>
#include <utility>
#include <rdma/fi_eq.h>
#include <rdma/fi_errno.h>
#include <sys/types.h>
#include "internal/Logging.hpp"
#include "Completion.hpp"
#include "Domain.hpp"
#include "Exception.hpp"

namespace mxl::lib::fabrics::ofi
{

    CompletionQueueAttr CompletionQueueAttr::defaults()
    {
        CompletionQueueAttr attr{};
        attr.size = 8;                    // default size, this should be parameterized
        attr.waitObject = FI_WAIT_UNSPEC; // TODO: EFA will require no wait object
        return attr;
    }

    ::fi_cq_attr CompletionQueueAttr::raw() const noexcept
    {
        ::fi_cq_attr raw{};
        raw.size = size;
        raw.wait_obj = waitObject;
        raw.format = FI_CQ_FORMAT_DATA;  // default format, this should be parameterized
        raw.wait_cond = FI_CQ_COND_NONE; // default condition, this should be parameterized
        raw.wait_set = nullptr;          // only used if wait_obj is FI_WAIT_SET
        raw.flags = 0;                   // if signaling vector is required, this should use the flag "FI_AFFINITY"
        raw.signaling_vector = 0;        // this should indicate the CPU core that interrupts associated with the cq should target.
        return raw;
    }

    std::shared_ptr<CompletionQueue> CompletionQueue::open(std::shared_ptr<Domain> domain, CompletionQueueAttr const& attr)
    {
        ::fid_cq* cq;
        auto cq_attr = attr.raw();

        fiCall(::fi_cq_open, "Failed to open completion queue", domain->raw(), &cq_attr, &cq, nullptr);

        // expose the private constructor to std::make_shared inside this function
        struct MakeSharedEnabler : public CompletionQueue
        {
            MakeSharedEnabler(::fid_cq* raw, std::shared_ptr<Domain> domain)
                : CompletionQueue(raw, domain)
            {}
        };

        return std::make_shared<MakeSharedEnabler>(cq, domain);
    }

    std::optional<Completion> CompletionQueue::read()
    {
        fi_cq_data_entry entry;

        ssize_t ret = fi_cq_read(_raw, &entry, 1);

        return handleReadResult(ret, entry);
    }

    std::optional<Completion> CompletionQueue::readBlocking(std::chrono::steady_clock::duration timeout)
    {
        auto timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
        if (timeoutMs == 0)
        {
            return read();
        }

        fi_cq_data_entry entry;

        ssize_t ret = fi_cq_sread(_raw, &entry, 1, nullptr, timeoutMs);
        return handleReadResult(ret, entry);
    }

    CompletionQueue::CompletionQueue(::fid_cq* raw, std::shared_ptr<Domain> domain)
        : _raw(raw)
        , _domain(std::move(domain))
    {}

    CompletionQueue::~CompletionQueue()
    {
        close();
    }

    ::fid_cq* CompletionQueue::raw() noexcept
    {
        return _raw;
    }

    ::fid_cq const* CompletionQueue::raw() const noexcept
    {
        return _raw;
    }

    void CompletionQueue::close()
    {
        if (_raw)
        {
            MXL_DEBUG("Closing completion queue");

            fiCall(::fi_close, "Failed to close completion queue", &_raw->fid);
            _raw = nullptr;
        }
    }

    std::optional<Completion> CompletionQueue::handleReadResult(ssize_t ret, ::fi_cq_data_entry const& entry)
    {
        if (ret == -FI_EAGAIN)
        {
            // No entry available
            return std::nullopt;
        }

        if (ret == -FI_EAVAIL)
        {
            // An entry is available but in the error queue
            ::fi_cq_err_entry err;
            fi_cq_readerr(_raw, &err, 0);

            return Completion{
                Completion::Error{err, this->shared_from_this()}
            };
        }

        if (ret < 0)
        {
            throw FIException::make(ret, "Failed to read completion from queue: {}", ::fi_strerror(ret));
        }

        return Completion{Completion::Data{entry}};
    }
}
