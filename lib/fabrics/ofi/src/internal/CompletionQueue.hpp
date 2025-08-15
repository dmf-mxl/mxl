#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <rdma/fi_eq.h>
#include "Completion.hpp"
#include "Domain.hpp"

namespace mxl::lib::fabrics::ofi
{

    /// A collection of attributes that can be used to configure the
    /// operation of newly created completion queues.
    struct CompletionQueueAttr
    {
        /// Returns the a CompletionQueueAttr with all of its attributes
        /// set to reasonable defaults.
        static CompletionQueueAttr defaults();

        /// Returns the raw libfabric version of this object.
        [[nodiscard]]
        ::fi_cq_attr raw() const noexcept;

        size_t size;                 /// Size of the queue.
        enum fi_wait_obj waitObject; /// The underlying wait object that should be used.
    };

    class CompletionQueue : public std::enable_shared_from_this<CompletionQueue>
    {
    public:
        /// Create a new completion queue in the specified domain.
        static std::shared_ptr<CompletionQueue> open(std::shared_ptr<Domain> domain,
            CompletionQueueAttr const& attr = CompletionQueueAttr::defaults());

        ~CompletionQueue();

        // No copying
        CompletionQueue(CompletionQueue const&) = delete;
        void operator=(CompletionQueue const&) = delete;

        /// Move-constructor. A moved-from completion queue can no longer be used.
        CompletionQueue(CompletionQueue&&) noexcept;

        /// Move-assignment operator. Moving into an existing completion queue will release all of is resources
        /// and take ownership of the resources of the moved-from completion queue.
        CompletionQueue& operator=(CompletionQueue&&);

        ::fid_cq* raw() noexcept;
        [[nodiscard]]
        ::fid_cq const* raw() const noexcept;

        /// Perform a non-blocking read on the completion queue. If no completion event is available in the queue
        /// returns std::nullopt
        std::optional<Completion> read();

        /// Perform a blocking read on the completion queue. This function will block until the specified timeout
        /// has elapsed, the process is signalled, or a completion is available on the queue.
        /// If no event was available after the timeout, the function returns std::nullopt. If the process is signalled
        /// while blocking on this functions, an exception will be thrown.
        std::optional<Completion> readBlocking(std::chrono::steady_clock::duration timeout);

    private:
        // Releases all underlying resources. This is called from the destructor and the move-assignment operator.
        void close();

        // The completion queue can only exists behind a shared_ptr. Some events need to carry a reference to the
        // completion queue. To prevent the queue from beeing released before all these events have be released, they own
        // a shared_ptr to the queue they have originated from.
        CompletionQueue(::fid_cq* raw, std::shared_ptr<Domain> domain);

        // Handle the result of a blocking or non-blocking read.
        std::optional<Completion> handleReadResult(ssize_t ret, ::fi_cq_data_entry const& entry);

        ::fid_cq* _raw;                  /// Raw resource reference.
        std::shared_ptr<Domain> _domain; /// The domain this queue was created into.
    };
}
