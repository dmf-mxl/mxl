// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PayloadBackend.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <limits>
#include <utility>
#include <dlfcn.h>
#include <unistd.h>
#include <fmt/format.h>
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

namespace mxl::lib
{
    namespace
    {
        /** Prefix of every backend library file name. */
        constexpr auto BACKEND_LIBRARY_PREFIX = std::string_view{"libmxl-payload-"};

        /** Suffix of every backend library file name. CMake gives MODULE libraries this suffix on Linux and macOS. */
        constexpr auto BACKEND_LIBRARY_SUFFIX = std::string_view{".so"};

        /** Longest accepted backend name. */
        constexpr auto MAX_BACKEND_NAME_LENGTH = std::size_t{64};

        /** Name of the built-in host storage in the payload options. No backend library can have it. */
        constexpr auto BUILT_IN_STORAGE_NAME_HOST = std::string_view{"host"};

        /**
         * A function that is part of the binary holding the loader. Its address locates that binary.
         */
        void locationAnchor() noexcept
        {}

        /**
         * Find the directory of the binary that contains this code, which is libmxl in a normal build.
         * \return The directory, or an empty path if it cannot be determined.
         */
        std::filesystem::path findOwnDirectory() noexcept
        {
            try
            {
                auto info = Dl_info{};
                // Converting a function pointer to void* is conditionally supported. It is required on POSIX
                // systems, which are the only systems MXL runs on.
                if ((::dladdr(reinterpret_cast<void const*>(&locationAnchor), &info) != 0) && (info.dli_fname != nullptr))
                {
                    return std::filesystem::weakly_canonical(std::filesystem::path{info.dli_fname}).parent_path();
                }
            }
            catch (std::exception const& e)
            {
                MXL_ERROR("Could not resolve the directory of libmxl: {}", e.what());
                return {};
            }
            // The default loader then searches only the directories of the plugin path variable.
            MXL_ERROR("Could not determine the directory of libmxl. Payload backends are only searched for in {}.",
                PayloadBackendLoader::PAYLOAD_PLUGIN_PATH_VARIABLE);
            return {};
        }

        /**
         * Read an environment variable, unless the process runs with raised privileges.
         * \param[in] name The variable name.
         * \return The value, or null if the variable is not set or the process is setuid or setgid.
         */
        char const* getenvUnlessPrivileged(char const* name) noexcept
        {
#ifdef __APPLE__
            return (::issetugid() != 0) ? nullptr : std::getenv(name);
#else
            return ::secure_getenv(name);
#endif
        }

        /**
         * \return The search directories of the default loader.
         */
        std::vector<std::filesystem::path> defaultSearchDirectories()
        {
            auto directories = std::vector<std::filesystem::path>{};
            if (auto const* const value = getenvUnlessPrivileged(PayloadBackendLoader::PAYLOAD_PLUGIN_PATH_VARIABLE); value != nullptr)
            {
                directories = PayloadBackendLoader::parsePluginPath(value);
            }
            if (auto ownDirectory = findOwnDirectory(); !ownDirectory.empty())
            {
                directories.push_back(std::move(ownDirectory));
            }
            return directories;
        }

        /**
         * Check a backend function table against the name it was loaded under.
         * \param[in] api The table returned by the backend.
         * \param[in] name The name the backend was loaded under.
         * \return An empty string if the table is valid, otherwise a description of the problem.
         */
        std::string validateApi(mxlPayloadBackendApi const& api, std::string_view name)
        {
            if (api.structSize < sizeof(mxlPayloadBackendApi))
            {
                return fmt::format("function table is {} bytes, expected at least {}", api.structSize, sizeof(mxlPayloadBackendApi));
            }
            if (api.abiVersion != MXL_PAYLOAD_BACKEND_ABI_VERSION)
            {
                return fmt::format("implements interface version {}, expected {}", api.abiVersion, MXL_PAYLOAD_BACKEND_ABI_VERSION);
            }
            if ((api.name == nullptr) || (name != api.name))
            {
                return fmt::format("reports name '{}'", (api.name != nullptr) ? api.name : "<null>");
            }
            if ((api.storageType == nullptr) || !isValidStorageTypeName(api.storageType))
            {
                return fmt::format("reports the invalid storage type '{}'", (api.storageType != nullptr) ? api.storageType : "<null>");
            }
            if ((api.prepare == nullptr) || (api.open == nullptr) || (api.destroy == nullptr) || (api.slotStorage == nullptr) ||
                (api.describeLayout == nullptr) || (api.mapSlots == nullptr))
            {
                return "does not provide every function";
            }
            if ((api.capabilities & ~(MXL_PAYLOAD_BACKEND_CAP_SHARE_DESCRIPTORS | MXL_PAYLOAD_BACKEND_CAP_DESCRIBE_GEOMETRY)) != 0U)
            {
                return fmt::format("reports unknown capabilities {:#x}", api.capabilities);
            }
            if (((api.capabilities & MXL_PAYLOAD_BACKEND_CAP_SHARE_DESCRIPTORS) != 0U) &&
                ((api.exportDescriptors == nullptr) || (api.importDescriptors == nullptr)))
            {
                return "shares descriptors but does not provide the functions to export and import them";
            }
            if (((api.capabilities & MXL_PAYLOAD_BACKEND_CAP_DESCRIBE_GEOMETRY) != 0U) && (api.describeGeometry == nullptr))
            {
                return "defines media types but does not provide the function that describes their geometry";
            }
            return {};
        }

        /**
         * Check a grain geometry a backend reported.
         * \param[in] geometry The backend's answer.
         * \return An empty string if the geometry is valid, otherwise a description of the problem.
         */
        std::string validateGeometry(mxlPayloadBackendGrainGeometry const& geometry)
        {
            if (geometry.structSize != sizeof(mxlPayloadBackendGrainGeometry))
            {
                return "changed the size of the geometry structure";
            }
            if ((geometry.planeCount == 0U) || (geometry.planeCount > MXL_MAX_PLANES_PER_GRAIN))
            {
                return fmt::format("reported {} planes, expected 1 to {}", geometry.planeCount, MXL_MAX_PLANES_PER_GRAIN);
            }
            // mxlGrainInfo counts slices in 16 bits.
            if ((geometry.slicesPerGrain == 0U) || (geometry.slicesPerGrain > std::numeric_limits<std::uint16_t>::max()))
            {
                return fmt::format(
                    "reported {} slices per grain, expected 1 to {}", geometry.slicesPerGrain, std::numeric_limits<std::uint16_t>::max());
            }
            auto grainSize = std::uint64_t{0};
            for (auto plane = std::uint32_t{0}; plane < MXL_MAX_PLANES_PER_GRAIN; ++plane)
            {
                if ((geometry.sliceSizes[plane] == 0U) != (plane >= geometry.planeCount))
                {
                    return fmt::format("reported a slice size of {} for plane {} of {}", geometry.sliceSizes[plane], plane, geometry.planeCount);
                }
                grainSize += std::uint64_t{geometry.sliceSizes[plane]} * geometry.slicesPerGrain;
            }
            // mxlGrainInfo holds the grain size in 32 bits.
            if (grainSize > std::numeric_limits<std::uint32_t>::max())
            {
                return fmt::format("reported a grain of {} bytes, more than {}", grainSize, std::numeric_limits<std::uint32_t>::max());
            }
            return {};
        }
    }

    GrainGeometry describeGrainGeometry(mxlPayloadBackendApi const& api, std::string const& flowDef, mxlDataFormat format, std::size_t grainLines)
    {
        if ((api.capabilities & MXL_PAYLOAD_BACKEND_CAP_DESCRIBE_GEOMETRY) == 0U)
        {
            throw PayloadStorageError{MXL_ERR_UNSUPPORTED_OPERATION,
                fmt::format("Payload backend '{}' stores only the media types libmxl defines, and this flow's media type is not one of them.",
                    api.name)};
        }

        auto const query = mxlPayloadBackendGeometryQuery{
            .structSize = sizeof(mxlPayloadBackendGeometryQuery),
            .format = static_cast<std::uint32_t>(format),
            .grainLines = static_cast<std::uint32_t>(grainLines),
            .reserved = 0U,
            .flowDefinition = flowDef.c_str(),
        };
        auto geometry = mxlPayloadBackendGrainGeometry{};
        geometry.structSize = sizeof(mxlPayloadBackendGrainGeometry);
        if (auto const status = api.describeGeometry(&query, &geometry); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status,
                (status == MXL_ERR_UNSUPPORTED_OPERATION)
                    ? fmt::format("Payload backend '{}' does not define this flow's media type.", api.name)
                    : fmt::format("Payload backend '{}' cannot describe the grain geometry of the flow.", api.name)};
        }
        if (auto const problem = validateGeometry(geometry); !problem.empty())
        {
            throw PayloadStorageError{
                MXL_ERR_INTERNAL, fmt::format("Payload backend '{}' returned an invalid grain geometry: it {}.", api.name, problem)};
        }

        auto result = GrainGeometry{.payloadSize = 0U, .totalSlices = geometry.slicesPerGrain, .sliceLengths = {}};
        std::ranges::copy(geometry.sliceSizes, result.sliceLengths.begin());
        for (auto const sliceLength : result.sliceLengths)
        {
            result.payloadSize += std::size_t{sliceLength} * result.totalSlices;
        }
        return result;
    }

    PayloadBackendLoader& PayloadBackendLoader::defaultLoader()
    {
        static auto loader = PayloadBackendLoader{defaultSearchDirectories()};
        return loader;
    }

    PayloadBackendLoader::PayloadBackendLoader(std::vector<std::filesystem::path> searchDirectories)
        : _searchDirectories{std::move(searchDirectories)}
        , _mutex{}
        , _backends{}
    {}

    PayloadBackendLoader::~PayloadBackendLoader() = default;

    mxlPayloadBackendApi const& PayloadBackendLoader::load(std::string const& name)
    {
        if (!isValidBackendName(name))
        {
            throw std::invalid_argument{fmt::format("Invalid payload backend name '{}'.", name)};
        }
        if (name == BUILT_IN_STORAGE_NAME_HOST)
        {
            throw std::invalid_argument{"The host payload storage is built into libmxl and is not loaded from a library."};
        }

        auto const lock = std::lock_guard{_mutex};
        if (auto const pos = _backends.find(name); pos != _backends.end())
        {
            return *pos->second;
        }

        auto const fileName = libraryFileName(name);
        auto const found = std::ranges::find_if(_searchDirectories,
            [&](std::filesystem::path const& directory)
            {
                auto ec = std::error_code{};
                return std::filesystem::is_regular_file(directory / fileName, ec);
            });
        if (found == _searchDirectories.end())
        {
            auto searched = std::string{};
            for (auto const& directory : _searchDirectories)
            {
                searched += (searched.empty() ? "" : ", ") + directory.string();
            }
            throw std::runtime_error{
                fmt::format("Cannot find payload backend '{}' ({}) in: {}.", name, fileName, searched.empty() ? "no directories" : searched)};
        }

        auto const path = *found / fileName;
        // The handle is intentionally never closed. Function tables and the code they point to must stay valid
        // for storage objects that may outlive any particular caller.
        auto* const handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            auto const* const error = ::dlerror();
            throw std::runtime_error{
                fmt::format("Cannot load payload backend '{}' from {}: {}", name, path.string(), (error != nullptr) ? error : "unknown error")};
        }

        auto* const symbol = ::dlsym(handle, MXL_PAYLOAD_BACKEND_ENTRY_POINT);
        if (symbol == nullptr)
        {
            ::dlclose(handle);
            throw std::runtime_error{fmt::format("Payload backend library {} does not export {}.", path.string(), MXL_PAYLOAD_BACKEND_ENTRY_POINT)};
        }

        // Converting the object pointer returned by dlsym to a function pointer is required to work on POSIX systems.
        auto const entryPoint = reinterpret_cast<mxlGetPayloadBackendApiFn>(symbol);
        mxlPayloadBackendApi const* api = nullptr;
        if (auto const status = entryPoint(MXL_PAYLOAD_BACKEND_ABI_VERSION, &api); (status != MXL_STATUS_OK) || (api == nullptr))
        {
            ::dlclose(handle);
            throw std::runtime_error{
                fmt::format("Payload backend library {} does not implement interface version {}.", path.string(), MXL_PAYLOAD_BACKEND_ABI_VERSION)};
        }

        if (auto const problem = validateApi(*api, name); !problem.empty())
        {
            ::dlclose(handle);
            throw std::runtime_error{fmt::format("Payload backend library {} is invalid: it {}.", path.string(), problem)};
        }

        MXL_DEBUG("Loaded payload backend '{}' from {}", name, path.string());
        _backends.emplace(name, api);
        return *api;
    }

    std::vector<std::filesystem::path> const& PayloadBackendLoader::searchDirectories() const noexcept
    {
        return _searchDirectories;
    }

    std::vector<std::filesystem::path> PayloadBackendLoader::parsePluginPath(std::string_view value)
    {
        auto directories = std::vector<std::filesystem::path>{};
        while (!value.empty())
        {
            auto const end = value.find(':');
            auto const entry = std::filesystem::path{value.substr(0, end)};
            if (entry.is_absolute())
            {
                directories.push_back(entry);
            }
            else if (!entry.empty())
            {
                MXL_WARN("Ignoring relative directory '{}' in {}.", entry.string(), PAYLOAD_PLUGIN_PATH_VARIABLE);
            }
            value = (end == std::string_view::npos) ? std::string_view{} : value.substr(end + 1);
        }
        return directories;
    }

    bool PayloadBackendLoader::isValidBackendName(std::string_view name) noexcept
    {
        auto const isLowerOrDigit = [](char c)
        {
            return ((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9'));
        };

        if (name.empty() || (name.size() > MAX_BACKEND_NAME_LENGTH) || !isLowerOrDigit(name.front()))
        {
            return false;
        }
        return std::ranges::all_of(name, [&](char c) { return isLowerOrDigit(c) || (c == '-'); });
    }

    std::string PayloadBackendLoader::libraryFileName(std::string_view name)
    {
        return fmt::format("{}{}{}", BACKEND_LIBRARY_PREFIX, name, BACKEND_LIBRARY_SUFFIX);
    }

    std::unique_ptr<PluginPayloadStorage> PluginPayloadStorage::prepare(mxlPayloadBackendApi const& api, mxlPayloadBackendFlowInfo const& info,
        std::string const& options, std::filesystem::path const& stagingDir, std::filesystem::path const& finalDir)
    {
        mxlPayloadBackendInstance instance = nullptr;
        if (auto const status = api.prepare(&info, options.empty() ? "{}" : options.c_str(), stagingDir.c_str(), finalDir.c_str(), &instance);
            status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to prepare the flow storage.", api.name)};
        }
        auto result = std::unique_ptr<PluginPayloadStorage>{
            new PluginPayloadStorage{api, instance, info.grainCount, {}}
        };
        if (result->sharesDescriptors())
        {
            result->serveDescriptors(stagingDir / PAYLOAD_SOCKET_FILE_NAME);
        }
        return result;
    }

    std::unique_ptr<PluginPayloadStorage> PluginPayloadStorage::open(mxlPayloadBackendApi const& api, mxlPayloadBackendFlowInfo const& info,
        std::string const& options, std::filesystem::path const& flowDir)
    {
        mxlPayloadBackendInstance instance = nullptr;
        if (auto const status = api.open(&info, options.empty() ? "{}" : options.c_str(), flowDir.c_str(), &instance); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to open the flow storage.", api.name)};
        }
        return std::unique_ptr<PluginPayloadStorage>{
            new PluginPayloadStorage{api, instance, info.grainCount, flowDir}
        };
    }

    PluginPayloadStorage::PluginPayloadStorage(mxlPayloadBackendApi const& api, mxlPayloadBackendInstance instance, std::size_t slotCount,
        std::filesystem::path readerFlowDir) noexcept
        : _api{&api}
        , _instance{instance}
        , _slotCount{slotCount}
        , _readerFlowDir{std::move(readerFlowDir)}
        , _server{}
        , _descriptorsMutex{}
        , _descriptorsReceived{false}
    {}

    PluginPayloadStorage::~PluginPayloadStorage()
    {
        // Stop serving before the backend closes the descriptors.
        _server.reset();
        _api->destroy(_instance);
    }

    bool PluginPayloadStorage::sharesDescriptors() const noexcept
    {
        return (_api->capabilities & MXL_PAYLOAD_BACKEND_CAP_SHARE_DESCRIPTORS) != 0U;
    }

    void PluginPayloadStorage::serveDescriptors(std::filesystem::path const& socketPath)
    {
        auto count = std::uint32_t{0};
        auto descriptors = std::vector<int>{};
        auto status = _api->exportDescriptors(_instance, nullptr, 0U, &count);
        if (status == MXL_STATUS_OK)
        {
            descriptors.resize(count, -1);
            status = _api->exportDescriptors(_instance, descriptors.data(), count, &count);
        }
        if ((status != MXL_STATUS_OK) || (count != descriptors.size()))
        {
            throw PayloadStorageError{(status != MXL_STATUS_OK) ? status : MXL_ERR_INTERNAL,
                fmt::format("Payload backend '{}' cannot export its descriptors.", _api->name)};
        }

        try
        {
            // Whoever may read the flow's data may receive its payload.
            _server = std::make_unique<DescriptorServer>(socketPath, std::move(descriptors), makeFlowDataFilePath(socketPath.parent_path()));
        }
        catch (std::exception const& e)
        {
            throw PayloadStorageError{MXL_ERR_UNKNOWN, e.what()};
        }
    }

    void PluginPayloadStorage::ensureDescriptors() const
    {
        if (!sharesDescriptors() || _readerFlowDir.empty() || _descriptorsReceived.load(std::memory_order_acquire))
        {
            return;
        }

        auto const lock = std::lock_guard{_descriptorsMutex};
        if (_descriptorsReceived.load(std::memory_order_relaxed))
        {
            return;
        }

        auto received = std::vector<UniqueFd>{};
        try
        {
            received = receiveDescriptors(_readerFlowDir / PAYLOAD_SOCKET_FILE_NAME);
        }
        catch (std::exception const& e)
        {
            // The usual cause is a writer that is gone, which no longer serves the socket.
            throw PayloadStorageError{MXL_ERR_INVALID_STATE, fmt::format("Cannot receive the payload descriptors from the writer: {}", e.what())};
        }

        // The backend takes ownership of every descriptor, whatever it returns.
        auto descriptors = std::vector<int>{};
        descriptors.reserve(received.size());
        for (auto& fd : received)
        {
            descriptors.push_back(fd.release());
        }
        if (auto const status = _api->importDescriptors(_instance, descriptors.data(), static_cast<std::uint32_t>(descriptors.size()));
            status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' rejected the payload descriptors.", _api->name)};
        }
        _descriptorsReceived.store(true, std::memory_order_release);
    }

    std::string_view PluginPayloadStorage::type() const noexcept
    {
        return _api->storageType;
    }

    std::size_t PluginPayloadStorage::slotCount() const noexcept
    {
        return _slotCount;
    }

    mxlGrainStorage PluginPayloadStorage::slotStorage(std::size_t slot) const
    {
        if (slot >= _slotCount)
        {
            throw std::out_of_range{fmt::format("Slot {} is out of range, the storage has {} slots.", slot, _slotCount)};
        }
        ensureDescriptors();

        auto result = mxlGrainStorage{};
        std::memset(&result, 0, sizeof(result));
        if (auto const status = _api->slotStorage(_instance, static_cast<std::uint32_t>(slot), &result); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot provide the storage of slot {}.", _api->name, slot)};
        }

        // Check that the backend filled the description it was asked for.
        if ((result.version != 1U) || (result.size != sizeof(mxlGrainStorage)) || (result.slot != slot) || (result.reserved0 != 0U))
        {
            throw PayloadStorageError{
                MXL_ERR_INTERNAL, fmt::format("Payload backend '{}' returned an invalid description of slot {}.", _api->name, slot)};
        }
        return result;
    }

    void PluginPayloadStorage::describeLayout(mxlGrainStorageLayout& layout) const
    {
        if (auto const status = _api->describeLayout(_instance, &layout); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot describe the storage layout.", _api->name)};
        }
    }

    void PluginPayloadStorage::mapSlots() const
    {
        ensureDescriptors();
        if (auto const status = _api->mapSlots(_instance); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot map the flow storage.", _api->name)};
        }
    }

    UnavailablePayloadStorage::UnavailablePayloadStorage(std::string type, std::size_t slotCount, mxlStatus status, std::string reason)
        : _type{std::move(type)}
        , _slotCount{slotCount}
        , _status{status}
        , _reason{std::move(reason)}
    {}

    std::string_view UnavailablePayloadStorage::type() const noexcept
    {
        return _type;
    }

    std::size_t UnavailablePayloadStorage::slotCount() const noexcept
    {
        return _slotCount;
    }

    mxlGrainStorage UnavailablePayloadStorage::slotStorage(std::size_t) const
    {
        throw PayloadStorageError{_status, _reason};
    }

    void UnavailablePayloadStorage::describeLayout(mxlGrainStorageLayout&) const
    {
        throw PayloadStorageError{_status, _reason};
    }

    void UnavailablePayloadStorage::mapSlots() const
    {
        throw PayloadStorageError{_status, _reason};
    }
}
