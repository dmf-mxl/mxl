// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <mxl/mxl.h>
#include <mxl/platform.h>
#include "PayloadBackendAbi.h"
#include "PayloadStorage.hpp"

namespace mxl::lib
{
    /**
     * Loads payload storage backends that ship with the SDK.
     *
     * A backend named "cuda-linear" is loaded from libmxl-payload-cuda-linear.so. The loader searches its
     * directories in order and uses the first one that holds that file. If that library is invalid, loading fails
     * and later directories are not searched. The default loader searches the directories listed in the
     * PAYLOAD_PLUGIN_PATH_VARIABLE environment variable, then the directory of the binary that contains libmxl.
     * There is no API to register backends.
     *
     * Libraries are loaded on first use and never unloaded, so function tables returned by load() stay valid
     * until the process exits. All member functions are thread safe.
     */
    class MXL_EXPORT PayloadBackendLoader
    {
    public:
        /**
         * Environment variable with directories to search for backends before the libmxl directory, separated by
         * colons. It is ignored in setuid and setgid processes.
         */
        constexpr static auto PAYLOAD_PLUGIN_PATH_VARIABLE = "MXL_PAYLOAD_PLUGIN_PATH";

        /**
         * The loader used by libmxl.
         * \return The loader that searches the directories of PAYLOAD_PLUGIN_PATH_VARIABLE, then the directory of
         *     the binary containing libmxl.
         */
        [[nodiscard]]
        static PayloadBackendLoader& defaultLoader();

        /**
         * Create a loader that searches the given directories in order. Used by the default loader and by tests.
         * \param[in] searchDirectories The directories that hold backend libraries.
         */
        explicit PayloadBackendLoader(std::vector<std::filesystem::path> searchDirectories);

        /** Destructor. Does not unload any library. */
        ~PayloadBackendLoader();

        PayloadBackendLoader(PayloadBackendLoader const&) = delete;
        PayloadBackendLoader(PayloadBackendLoader&&) = delete;
        PayloadBackendLoader& operator=(PayloadBackendLoader const&) = delete;
        PayloadBackendLoader& operator=(PayloadBackendLoader&&) = delete;

        /**
         * Load a backend, or return the one already loaded under that name.
         *
         * The library must export MXL_PAYLOAD_BACKEND_ENTRY_POINT, implement MXL_PAYLOAD_BACKEND_ABI_VERSION,
         * report the requested name, provide every function, and report a known storage type.
         *
         * \param[in] name The backend name.
         * \return The function table of the backend. Valid until the process exits.
         * \throws std::invalid_argument if name is not a valid backend name, or is the name of a built-in storage.
         * \throws std::runtime_error if no search directory holds the library, or the first one found cannot be
         *     loaded or fails validation.
         */
        [[nodiscard]]
        mxlPayloadBackendApiV1 const& load(std::string const& name);

        /**
         * \return The directories this loader searches, in order.
         */
        [[nodiscard]]
        std::vector<std::filesystem::path> const& searchDirectories() const noexcept;

        /**
         * Split the value of PAYLOAD_PLUGIN_PATH_VARIABLE into directories. Empty and relative entries are
         * skipped, because a relative entry would depend on the working directory of the process.
         *
         * \param[in] value The value of the variable.
         * \return The absolute directories, in the order given.
         */
        [[nodiscard]]
        static std::vector<std::filesystem::path> parsePluginPath(std::string_view value);

        /**
         * Check whether a string can name a backend: 1 to 64 characters, lowercase ASCII letters, digits and
         * hyphens, starting with a letter or digit. This keeps names from forming paths.
         *
         * \param[in] name The candidate name.
         * \return true if name is a valid backend name.
         */
        [[nodiscard]]
        static bool isValidBackendName(std::string_view name) noexcept;

        /**
         * \param[in] name A valid backend name.
         * \return The file name of the library that implements the backend.
         */
        [[nodiscard]]
        static std::string libraryFileName(std::string_view name);

    private:
        /** The directories that hold backend libraries, in search order. */
        std::vector<std::filesystem::path> _searchDirectories;
        /** Protects _backends. */
        std::mutex _mutex;
        /** Loaded backends by name. The libraries stay mapped for the life of the process. */
        std::map<std::string, mxlPayloadBackendApiV1 const*, std::less<>> _backends;
    };

    /**
     * Payload storage implemented by a backend library.
     *
     * Owns one backend instance and releases it on destruction.
     */
    class MXL_EXPORT
    PluginPayloadStorage final : public PayloadStorage
    {
    public:
        /**
         * Create the payload storage of a new flow. See mxlPayloadBackendApiV1::prepare.
         *
         * \param[in] api The backend function table.
         * \param[in] info The flow geometry.
         * \param[in] options The writer's options for the backend, as JSON object text.
         * \param[in] stagingDir The temporary flow directory.
         * \param[in] finalDir The directory the flow will have once published.
         * \return The new storage.
         * \throws PayloadStorageError with the backend's status if the backend fails.
         */
        [[nodiscard]]
        static std::unique_ptr<PluginPayloadStorage> prepare(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendFlowInfo const& info,
            std::string const& options, std::filesystem::path const& stagingDir, std::filesystem::path const& finalDir);

        /**
         * Open the payload storage of an existing flow. Does not import the payload.
         * See mxlPayloadBackendApiV1::open.
         *
         * \param[in] api The backend function table.
         * \param[in] info The flow geometry and the access this process needs.
         * \param[in] options The reader's options for the backend, as JSON object text.
         * \param[in] flowDir The published flow directory.
         * \return The storage.
         * \throws PayloadStorageError with the backend's status if the backend fails.
         */
        [[nodiscard]]
        static std::unique_ptr<PluginPayloadStorage> open(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendFlowInfo const& info,
            std::string const& options, std::filesystem::path const& flowDir);

        /** Destructor. Releases the backend instance. */
        ~PluginPayloadStorage() override;

        /** \see PayloadStorage::type() */
        [[nodiscard]]
        mxlPayloadStorageType type() const noexcept override;

        /** \see PayloadStorage::deviceIndex() */
        [[nodiscard]]
        std::int32_t deviceIndex() const noexcept override;

        /** \see PayloadStorage::slotCount() */
        [[nodiscard]]
        std::size_t slotCount() const noexcept override;

        /**
         * \see PayloadStorage::slotStorage()
         * \throws PayloadStorageError if the backend cannot import the payload or returns an invalid description.
         */
        [[nodiscard]]
        mxlGrainStorage slotStorage(std::size_t slot) const override;

        /**
         * \see PayloadStorage::describeLayout()
         * \throws PayloadStorageError if the backend cannot describe the layout.
         */
        void describeLayout(mxlGrainStorageLayout& layout) const override;

        /**
         * \see PayloadStorage::mapSlots()
         * \throws PayloadStorageError if the backend cannot map the storage.
         */
        void mapSlots() const override;

    private:
        /**
         * \param[in] api The backend function table.
         * \param[in] instance The backend instance. Ownership passes to the new object.
         * \param[in] slotCount The number of slots of the flow.
         */
        PluginPayloadStorage(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendInstance instance, std::size_t slotCount) noexcept;

        /**
         * Listen on socketPath and hand each connected reader a copy of fds.
         * \param[in] socketPath Path of the socket in the published flow directory. The socket inode is bound at
         *      bindPath, which is the same file before the flow directory is renamed into place.
         * \param[in] bindPath Where to bind the socket now.
         * \param[in] fds Descriptors to serve. This object owns them.
         */
        void serveDescriptors(std::filesystem::path socketPath, std::filesystem::path bindPath, std::vector<int> fds);

        /** The backend function table. Valid until the process exits. */
        mxlPayloadBackendApiV1 const* _api;
        /** The backend instance owned by this object. */
        mxlPayloadBackendInstance _instance;
        /** The number of slots of the flow. */
        std::size_t _slotCount;
        /** Path of payload.sock in the published flow directory. Empty when the backend shares no descriptors. */
        std::filesystem::path _descriptorSocket;
        /** Serves _descriptorSocket for a writer. */
        std::unique_ptr<struct PayloadFdServer> _fdServer;
    };

    /**
     * Stands in for the storage of a flow whose backend cannot be used in this process, for example because the
     * backend library is not installed or its device runtime is missing.
     *
     * A reader can still open the flow and read its metadata. Every access to the payload storage throws
     * PayloadStorageError with the status that explains why the backend is not available.
     */
    class MXL_EXPORT
    UnavailablePayloadStorage final : public PayloadStorage
    {
    public:
        /**
         * \param[in] type The storage type recorded for the flow.
         * \param[in] slotCount The number of slots of the flow.
         * \param[in] status The status to report for every access to the payload storage.
         * \param[in] reason A description of why the backend is not available.
         */
        UnavailablePayloadStorage(mxlPayloadStorageType type, std::size_t slotCount, mxlStatus status, std::string reason);

        /** \see PayloadStorage::type() */
        [[nodiscard]]
        mxlPayloadStorageType type() const noexcept override;

        /** \see PayloadStorage::deviceIndex() */
        [[nodiscard]]
        std::int32_t deviceIndex() const noexcept override;

        /** \see PayloadStorage::slotCount() */
        [[nodiscard]]
        std::size_t slotCount() const noexcept override;

        /**
         * \see PayloadStorage::slotStorage()
         * \throws PayloadStorageError always.
         */
        [[nodiscard]]
        mxlGrainStorage slotStorage(std::size_t slot) const override;

        /**
         * \see PayloadStorage::describeLayout()
         * \throws PayloadStorageError always.
         */
        void describeLayout(mxlGrainStorageLayout& layout) const override;

        /**
         * \see PayloadStorage::mapSlots()
         * \throws PayloadStorageError always.
         */
        void mapSlots() const override;

    private:
        /** The storage type recorded for the flow. */
        mxlPayloadStorageType _type;
        /** The number of slots of the flow. */
        std::size_t _slotCount;
        /** The status reported for every access. */
        mxlStatus _status;
        /** Why the backend is not available. */
        std::string _reason;
    };
}
