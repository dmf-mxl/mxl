// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PayloadBackend.hpp"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <utility>
#include <dlfcn.h>
#include <unistd.h>
#include <fmt/format.h>
#include "mxl-internal/Logging.hpp"

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

        /** Storage types built into libmxl. Their names cannot be used by a backend library. */
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
        std::string validateApi(mxlPayloadBackendApiV1 const& api, std::string_view name)
        {
            if (api.structSize < sizeof(mxlPayloadBackendApiV1))
            {
                return fmt::format("function table is {} bytes, expected at least {}", api.structSize, sizeof(mxlPayloadBackendApiV1));
            }
            if (api.abiVersion != MXL_PAYLOAD_BACKEND_ABI_VERSION)
            {
                return fmt::format("implements interface version {}, expected {}", api.abiVersion, MXL_PAYLOAD_BACKEND_ABI_VERSION);
            }
            if ((api.name == nullptr) || (name != api.name))
            {
                return fmt::format("reports name '{}'", (api.name != nullptr) ? api.name : "<null>");
            }
            if (api.storageType != MXL_PAYLOAD_STORAGE_HOST_POINTER)
            {
                return fmt::format("reports unknown storage type {}", api.storageType);
            }
            if ((api.prepare == nullptr) || (api.open == nullptr) || (api.destroy == nullptr) || (api.deviceIndex == nullptr) ||
                (api.slotStorage == nullptr) || (api.describeLayout == nullptr) || (api.mapSlots == nullptr))
            {
                return "does not provide every function";
            }
            return {};
        }
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

    mxlPayloadBackendApiV1 const& PayloadBackendLoader::load(std::string const& name)
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
        mxlPayloadBackendApiV1 const* api = nullptr;
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

    std::unique_ptr<PluginPayloadStorage> PluginPayloadStorage::prepare(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendFlowInfo const& info,
        std::string const& options, std::filesystem::path const& stagingDir, std::filesystem::path const& finalDir)
    {
        mxlPayloadBackendInstance instance = nullptr;
        if (auto const status = api.prepare(&info, options.empty() ? "{}" : options.c_str(), stagingDir.c_str(), finalDir.c_str(), &instance);
            status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to prepare the flow storage.", api.name)};
        }
        return std::unique_ptr<PluginPayloadStorage>{
            new PluginPayloadStorage{api, instance, info.grainCount}
        };
    }

    std::unique_ptr<PluginPayloadStorage> PluginPayloadStorage::open(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendFlowInfo const& info,
        std::string const& options, std::filesystem::path const& flowDir)
    {
        mxlPayloadBackendInstance instance = nullptr;
        if (auto const status = api.open(&info, options.empty() ? "{}" : options.c_str(), flowDir.c_str(), &instance); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' failed to open the flow storage.", api.name)};
        }
        return std::unique_ptr<PluginPayloadStorage>{
            new PluginPayloadStorage{api, instance, info.grainCount}
        };
    }

    PluginPayloadStorage::PluginPayloadStorage(mxlPayloadBackendApiV1 const& api, mxlPayloadBackendInstance instance, std::size_t slotCount) noexcept
        : _api{&api}
        , _instance{instance}
        , _slotCount{slotCount}
    {}

    PluginPayloadStorage::~PluginPayloadStorage()
    {
        _api->destroy(_instance);
    }

    mxlPayloadStorageType PluginPayloadStorage::type() const noexcept
    {
        return static_cast<mxlPayloadStorageType>(_api->storageType);
    }

    std::int32_t PluginPayloadStorage::deviceIndex() const noexcept
    {
        return _api->deviceIndex(_instance);
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

        auto result = mxlGrainStorage{};
        std::memset(&result, 0, sizeof(result));
        if (auto const status = _api->slotStorage(_instance, static_cast<std::uint32_t>(slot), &result); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot provide the storage of slot {}.", _api->name, slot)};
        }

        // Check that the backend filled the description it was asked for.
        if ((result.version != 1U) || (result.size != sizeof(mxlGrainStorage)) || (result.storageType != _api->storageType) || (result.slot != slot))
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
        if (auto const status = _api->mapSlots(_instance); status != MXL_STATUS_OK)
        {
            throw PayloadStorageError{status, fmt::format("Payload backend '{}' cannot map the flow storage.", _api->name)};
        }
    }
}
