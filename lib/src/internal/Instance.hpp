#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <uuid.h>
#include <mxl/mxl.h>
#include "DomainWatcher.hpp"
#include "FlowIoFactory.hpp"
#include "FlowManager.hpp"

namespace mxl::lib
{
    ///
    /// Manages mxl resources allocated by the SDK user.  Represents a single mxfInstance.
    ///
    class Instance
    {
    public:
        ///
        /// Creates an instance
        /// \param[in] mxlDomain The directory where the shared memory files will be created
        /// \param[in] options Additional options. \todo Not implemented yet.
        /// \param[in] flowIoFactory A factory used to create flow readers for flows of different types.
        ///
        Instance(std::filesystem::path const& mxlDomain, std::string const& options, std::unique_ptr<FlowIoFactory>&& flowIoFactory);

        /// Dtor. Release all readers and writers.
        ~Instance();

        Instance(Instance&&) = delete;
        Instance(Instance const&) = delete;
        Instance& operator=(Instance&&) = delete;
        Instance& operator=(Instance const&) = delete;

        ///
        /// Create a flow
        ///
        /// \param[in] flowDef The json flow definition according to the NMOS Flow Resource json schema
        /// \return The created flow resources in shared memory
        /// \throw std::runtime_error On any error (parse exception, shared memory conflicts, etc)
        ///
        std::unique_ptr<FlowData> createFlow(std::string const& flowDef);

        /// Delete a flow by id
        ///
        /// \param[in] flowId The flow id
        /// \return false if the flow was not found.
        bool deleteFlow(uuids::uuid const& flowId);

        ///
        /// Create a FlowReader or obtain an additional reference to a
        /// previously created FlowReader.
        /// \param[in] flowId The id of the flow to obtain a reader for
        /// \return A pointer to the created flow reader.
        /// \note Please note that each successful call to this method must be
        ///     paired with a corresponding call to releaseReader().
        ///
        FlowReader* getFlowReader(std::string const& flowId);

        ///
        /// Release a reference to a FlowReader in order to ultimately free all
        /// resources associated with it, once the last reference is dropped.
        ///
        /// \param[in] reader a pointer to a reader previously obtained by a
        ///     call to getFlowReader.
        ///
        void releaseReader(FlowReader* reader);

        ///
        /// Create a FlowWriter or obtain an additional reference to a
        /// previously created FlowWriter.
        /// \param[in] flowId The id of the flow to obtain a reader for
        /// \return A pointer to the created flow reader.
        /// \note Please note that each successful call to this method must be
        ///     paired with a corresponding call to releaseWriter().
        ///
        FlowWriter* getFlowWriter(std::string const& flowId);

        ///
        /// Release a reference to a FlowWriter in order to ultimately free all
        /// resources associated with it, once the last reference is dropped.
        ///
        /// \param[in] writer a pointer to a writer previously obtained by a
        ///     call to getFlowWriter.
        ///
        void releaseWriter(FlowWriter* writer);

        ///
        /// Garbage collect the inactive flows.  This will remove any flows that are not being used by any readers or writers.
        /// \return The number of flows that were removed.
        ///
        std::size_t garbageCollect() const;

        /// Accessor for the history duration value
        /// \return The history duration in nanoseconds
        std::uint64_t getHistoryDurationNs() const;

    private:
        template<typename T>
        class RefCounted
        {
        public:
            using value_type = T;

        public:
            explicit RefCounted(std::unique_ptr<value_type>&& value) noexcept;

            RefCounted(RefCounted&&) = delete;
            RefCounted(RefCounted const&) = delete;
            RefCounted& operator=(RefCounted&&) = delete;
            RefCounted& operator=(RefCounted const&) = delete;

            constexpr value_type* get() noexcept;
            constexpr value_type const* get() const noexcept;

            constexpr void addReference() const noexcept;
            constexpr bool releaseReference() const noexcept;

            constexpr std::uint64_t referenceCount() const noexcept;

        private:
            std::unique_ptr<value_type> _ptr;
            std::uint64_t mutable _refCount;
        };

    private:
        void fileChangedCallback(uuids::uuid const& flowId, WatcherType type);

        /// Parses the options json string (if non empty) and merges it with the optional
        /// domain-wide options (if defined in the domain).
        /// Values defined at the instance level will override the domain-wide value.
        ///
        /// \param options The options json string
        void parseOptions(std::string const& options);

    private:
        /// Performs flow CRUD operations
        FlowManager _flowManager;

        /// The I/O factor used to delegate the creation of readers and writers
        std::unique_ptr<FlowIoFactory> _flowIoFactory;

        /// Maps flow uuids to flow readers.
        std::map<uuids::uuid, RefCounted<FlowReader>> _readers;
        /// Maps flow uuids to flow writers.
        std::map<uuids::uuid, RefCounted<FlowWriter>> _writers;

        /// Protects the maps
        std::mutex _mutex;

        /// For future use.
        std::string _options;

        /// Ring buffer history duration in nanoseconds
        std::uint64_t _historyDuration;

        DomainWatcher::ptr _watcher;

        std::atomic_bool _stopping;
    };

    /// Utility function to convert from a C mxlInstance handle to a C++ Instance class
    Instance* to_Instance(mxlInstance instance) noexcept;

    /// Utility function to convert from a C mxlFlowReader handle to a C++ FlowReaders instance.
    FlowReader* to_FlowReader(mxlFlowReader reader) noexcept;

    /// Utility function to convert from a C mxlFlowWriter handle to a C++ FlowWriter instance.
    FlowWriter* to_FlowWriter(mxlFlowWriter writer) noexcept;

    /**************************************************************************/
    /* Inline implementation.                                                 */
    /**************************************************************************/

    template<typename T>
    inline Instance::RefCounted<T>::RefCounted(std::unique_ptr<value_type>&& value) noexcept
        : _ptr{std::move(value)}
        , _refCount{1U}
    {}

    template<typename T>
    constexpr auto Instance::RefCounted<T>::get() noexcept -> value_type*
    {
        return _ptr.get();
    }

    template<typename T>
    constexpr auto Instance::RefCounted<T>::get() const noexcept -> value_type const*
    {
        return _ptr.get();
    }

    template<typename T>
    constexpr void Instance::RefCounted<T>::addReference() const noexcept
    {
        _refCount += 1U;
    }

    template<typename T>
    constexpr bool Instance::RefCounted<T>::releaseReference() const noexcept
    {
        _refCount -= (_refCount > 0U) ? 1U : 0U;
        return (_refCount == 0U);
    }

    template<typename T>
    constexpr std::uint64_t Instance::RefCounted<T>::referenceCount() const noexcept
    {
        return _refCount;
    }

    inline Instance* to_Instance(mxlInstance instance) noexcept
    {
        return reinterpret_cast<Instance*>(instance);
    }

    inline FlowReader* to_FlowReader(mxlFlowReader reader) noexcept
    {
        return reinterpret_cast<FlowReader*>(reader);
    }

    inline FlowWriter* to_FlowWriter(mxlFlowWriter writer) noexcept
    {
        return reinterpret_cast<FlowWriter*>(writer);
    }

} // namespace mxl::lib