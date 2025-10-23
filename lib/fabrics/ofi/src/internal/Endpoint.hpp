// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <uuid.h>
#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include "Address.hpp"
#include "AddressVector.hpp"
#include "Completion.hpp"
#include "CompletionQueue.hpp"
#include "Domain.hpp"
#include "EventQueue.hpp"
#include "FabricInfo.hpp"
#include "LocalRegion.hpp"
#include "RemoteRegion.hpp"

namespace mxl::lib::fabrics::ofi
{
    class Endpoint
    {
    public:
        // The miniumum interval at which the event queue will be polled when
        // doing a blocking read of both (event and completion) queues.
        constexpr static auto const EQReadInterval = std::chrono::milliseconds(100);

        // An id type that can be used to reference this endpoint from completions and events
        using Id = std::uintptr_t;

    public:
        // Generate a new random endpoint id.
        static Id randomId() noexcept;

        // Convert between Ids and context values that can be written to and read from raw libfabric queue entries and fid_t.
        static void* idToContextValue(Id id) noexcept;
        static Id contextValueToId(void*) noexcept;

        // Read the endpoint if from a libfarbic object reference. The result is only guaranteed to be valid if the fid
        // refers to a libfabric endpoint that was created with Endpoint::create()
        static Id idFromFID(::fid_t) noexcept;
        static Id idFromFID(::fid_ep*) noexcept;

        // Destructor releases all resources. If the event or completion queue have events pending that reference this endpoint, they still will be
        // delivered, but the reference to the endpoint inside these events can no longer be used.
        ~Endpoint();

        // Copy constructor is deleted
        Endpoint(Endpoint const&) = delete;
        void operator=(Endpoint const&) = delete;

        // Implements proper move semantics. An endpoint in a moved-from state can no longer be used.
        Endpoint(Endpoint&&) noexcept;
        // Move-assigning an endpoint to another releases all resources from the moved-into endpoint and
        Endpoint& operator=(Endpoint&&);

        /// Allocates (active) new endpoint. If the endpoint is allocated in order to accept a connection request. The
        /// fabric information obtained when accepting the connection must be passed via the info parameter.
        /// A random id will be chosen to identify the endpoint in completions and events.
        static Endpoint create(std::shared_ptr<Domain> domain, FabricInfoView info);

        // Allocates a new (active) endpoint. A new random id will be chosen to identify the endpoint in completions and events.
        static Endpoint create(std::shared_ptr<Domain> domain);

        /// Allocates (active) new endpoint. If the endpoint is allocated in order to accept a connection request. The
        /// fabric information obtained when accepting the connection must be passed via the info parameter.
        static Endpoint create(std::shared_ptr<Domain> domain, Id id, FabricInfoView);

        /// Allocates an (active) endpoint.
        static Endpoint create(std::shared_ptr<Domain> domain, Id id);

        /// Get the id of this endpoint. The id can be used to reference the endpoint from completion and regular events.
        [[nodiscard]]
        Id id() const noexcept;

        /// Bind the endpoint to an event queue. The endpoint can be bound to an event queue only once. The Endpoint object will take ownership of
        /// an instance of the shared pointer, so the queue can not be destroyed before the endpoint.
        void bind(std::shared_ptr<EventQueue> eq);

        /// Bind the endpoint to a completion queue. The endpoint can be bound to an event queue only once. The Endpoint object will take ownership of
        /// an instance of the shared pointer, so the queue can not be destroyed before the endpoint.
        void bind(std::shared_ptr<CompletionQueue> cq, std::uint64_t flags);

        /// Bind the endpoint to an address vector. The endpoint can be bound to an address vector only once. The Endpoint object will take ownership
        /// of an instance of the shared pointer, so the address vector can not be destroyed before the endpoint.
        void bind(std::shared_ptr<AddressVector> av);

        /// This  call  transitions  the  endpoint into an enabled state.  An endpoint must be enabled before it may be used to perform data
        /// transfers. Enabling an endpoint typically results in hardware resources being assigned to it.
        /// Endpoints making use of completion queues, event queues, and/or address vectors must be bound to them before being enabled.
        void enable();

        /// Accept an incoming connection. Information about the incoming connection must previously have been passed to Endpoint::create().
        /// A successfully accepted connection request will result in the active (connecting) endpoint seeing an Event::Connected event on its
        /// associated event queue. A rejected or failed connection request will generate an error event. The error entry will provide additional
        /// details describing the reason for the failed attempt. An Event::Connected event will also be generated on the passive side for the
        /// accepting endpoint once the connection has been properly established. The fid of the FI_CONNECTED event will be that of the endpoint
        /// passed to fi_accept as opposed to the listening passive endpoint. Outbound data transfers cannot be initiated on a connection-oriented
        /// endpoint until an FI_CONNECTED event has been generated. However, receive buffers may be associated with an endpoint anytime
        void accept();

        /// Initiate a connection to a remote (passive) endpoint. This function can only be called once in the lifetime of an Endpoint. After a
        /// connection was established,
        /// A Event::Connected event will be posted to the event queue associated with this endpoint.
        void connect(FabricAddress const& addr);

        /// Initiate a graceful shutdown on a connection-oriented endpoint. When the shutdown is complete, a Event::Shutdown event will be posted to
        /// the event queue associated with this endpoint.
        void shutdown();

        /// Obtain the local fabric address for this endpoint.
        [[nodiscard]]
        FabricAddress localAddress() const;

        /// If a completion queue is associated with this endpoint. Get the queue. Throws an exception if no completion queue is
        /// associated with this endpoint.
        [[nodiscard]]
        std::shared_ptr<CompletionQueue> completionQueue() const;

        /// If an event queue is associated with this endpoint. Get the queue. Throws an exception if no event queue is
        /// associated with this endpoint.
        [[nodiscard]]
        std::shared_ptr<EventQueue> eventQueue() const;

        /// If an address vector is associated with this endpoint. Get the AV. Throws an exception if no address vector is
        /// associated with this endpoint
        [[nodiscard]]
        std::shared_ptr<AddressVector> addressVector() const;

        /// Do a non-blocking read of both the event and completion queue associated with this endpoint.
        std::pair<std::optional<Completion>, std::optional<Event>> readQueues();

        /// Do a combined blocking read of both the event and completion queue associated with this endpoint.
        /// This function will do a non-blocking read of the event queue in an interval of 100ms and block on the completion queue the rest of the time.
        std::pair<std::optional<Completion>, std::optional<Event>> readQueuesBlocking(std::chrono::steady_clock::duration timeout);

        /// Get the domain that the endpoint has beed created on.
        [[nodiscard]]
        std::shared_ptr<Domain> domain() const;

        /// Get the fabric information passed to Endpoint::create() initially.
        [[nodiscard]]
        FabricInfoView info() const noexcept;

        /**
         * Push a remote write work request to the endpoint work queue. When the write is complete, a Completion::Data will be pushed to the
         * completion queue associated with the endpoint. Before a write request can be made, the endpoint must have beed enabled,
         * \param local Source memory region to write from
         * \param remoteGroup Destination memory regions to write to
         * \param destAddr The destination address of the target endpoint. This is unused when using connected endpoints.
         * \param 64 bits of user data that will be available in the completion entry associated with this transfer.
         */
        void write(LocalRegion const& local, RemoteRegion const& remote, ::fi_addr_t destAddr = FI_ADDR_UNSPEC,
            std::optional<std::uint32_t> immData = std::nullopt);

        /**
         * Push a remote write work request to the endpoint work queue. When the write is complete, a Completion::Data will be pushed to the
         * completion queue associated with the endpoint. Before a write request can be made, the endpoint must have beed enabled,
         * \param localGroup Source memory regions to write from (scatter-gather version)
         * \param remoteGroup Destination memory regions to write to
         * \param destAddr The destination address of the target endpoint. This is unused when using connected endpoints.
         * \param 64 bits of user data that will be available in the completion entry associated with this transfer.
         */
        void write(LocalRegionGroup const& localGroup, RemoteRegion const& remote, ::fi_addr_t destAddr = FI_ADDR_UNSPEC,
            std::optional<std::uint32_t> immData = std::nullopt);

        /*
         * Push a recv work request to the endpoint work queue. In the MXL context the memory region passed here is not the memory
         * to which the graines will be written, but instead a memory region of exact sizeof(uint64_t) bytes to which the grain index
         * of the grain that was trasferred last will be written.
         * \param region Source memory region to receive data into
         */
        void recv(LocalRegion region);

        /**
         * Get the raw libfabric handle to this endpoint.
         */
        ::fid_ep* raw() noexcept;

        /**
         * Get the raw libfabric handle to this endpoint.
         */
        [[nodiscard]]
        ::fid_ep const* raw() const noexcept;

    private:
        /// Internal implementation of the  remote write request
        void writeImpl(::iovec const* msgIov, std::size_t iovCount, void** desc, ::fi_rma_iov const* rmaIov, ::fi_addr_t destAddr,
            std::optional<std::uint32_t> immData);

        /// Close the endpoint and release all resources. Called from the destructor and the move assignment operator.
        void close();

        /// Construct the endpoint.
        Endpoint(::fid_ep* raw, FabricInfoView info, std::shared_ptr<Domain> domain,
            std::optional<std::shared_ptr<CompletionQueue>> cq = std::nullopt, std::optional<std::shared_ptr<EventQueue>> eq = std::nullopt,
            std::optional<std::shared_ptr<AddressVector>> av = std::nullopt);

    private:
        ::fid_ep* _raw;                                      /// Raw resource reference
        FabricInfo _info;                                    /// Info passed via Endpoint::create()
        std::shared_ptr<Domain> _domain;                     /// Domain in which the endpoint was created

        std::optional<std::shared_ptr<CompletionQueue>> _cq; /// Completion queue lives here after Endpoint::bind()
        std::optional<std::shared_ptr<EventQueue>> _eq;      /// Event queue lives here after Endpoint::bind()
        std::optional<std::shared_ptr<AddressVector>> _av;   /// Address vector lives here after Endpoint::bind()
    };
}
