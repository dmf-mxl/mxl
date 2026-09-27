// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include "mxl-internal/PayloadBackendRegistry.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <dlfcn.h>
#include <picojson/picojson.h>
#include <mxl/payload_plugin.h>
#include "mxl-internal/Logging.hpp"
#include "mxl-internal/PathUtils.hpp"

namespace mxl::lib
{
    namespace
    {
        bool isBuiltinBackendName(std::string const& name)
        {
            return (name == PAYLOAD_BACKEND_HOST) || (name == PAYLOAD_BACKEND_PLACEHOLDER);
        }

        int accessModeToPlugin(AccessMode mode)
        {
            switch (mode)
            {
                case AccessMode::READ_ONLY: return MXL_PAYLOAD_ACCESS_READ_ONLY;
                case AccessMode::READ_WRITE: return MXL_PAYLOAD_ACCESS_READ_WRITE;
                case AccessMode::CREATE_READ_WRITE: return MXL_PAYLOAD_ACCESS_CREATE_READ_WRITE;
            }
            return MXL_PAYLOAD_ACCESS_READ_ONLY;
        }

        class PluginPayloadAllocator final : public GrainPayloadAllocator
        {
        public:
            PluginPayloadAllocator(mxlPayloadBackendApiV1 const* api, GrainPayloadAllocatorSpec const& spec)
                : _api{api}
                , _location{spec.location}
                , _deviceIndex{spec.localDeviceIndex >= 0 ? spec.localDeviceIndex : spec.deviceIndex}
                , _backend{spec.backend}
            {
                auto info = mxlPayloadBackendCreateInfo{};
                info.struct_size = sizeof info;
                info.location = static_cast<int32_t>(spec.location);
                info.device_index = spec.deviceIndex;
                info.local_device_index = spec.localDeviceIndex;
                info.logical_payload_size = spec.logicalPayloadSize;

                auto const status = _api->create(&info, &_handle);
                if ((status != MXL_STATUS_OK) || (_handle == nullptr))
                {
                    throw std::runtime_error{"Payload plugin \"" + _backend + "\" failed to create a backend instance."};
                }
            }

            ~PluginPayloadAllocator() override
            {
                if ((_handle != nullptr) && (_api->destroy != nullptr))
                {
                    _api->destroy(_handle);
                }
            }

            PluginPayloadAllocator(PluginPayloadAllocator const&) = delete;
            PluginPayloadAllocator& operator=(PluginPayloadAllocator const&) = delete;

            [[nodiscard]]
            mxlPayloadLocation location() const noexcept override
            {
                return _location;
            }

            [[nodiscard]]
            int32_t deviceIndex() const noexcept override
            {
                return _deviceIndex;
            }

            [[nodiscard]]
            char const* backendName() const noexcept override
            {
                return _backend.c_str();
            }

            [[nodiscard]]
            std::size_t mappedPayloadBytes() const noexcept override
            {
                if ((_handle == nullptr) || (_api->mapped_payload_bytes == nullptr))
                {
                    return 0U;
                }
                return static_cast<std::size_t>(_api->mapped_payload_bytes(_handle));
            }

            void attach(GrainPayloadAttachContext const& context) override
            {
                auto pluginJson = std::string{};
                if (context.accessMode == AccessMode::CREATE_READ_WRITE)
                {
                    pluginJson = readPluginDescriptor();
                    writeEnvelope(context, pluginJson);
                }
                else
                {
                    pluginJson = readExistingPluginObject(context.flowDir);
                }

                auto info = mxlPayloadAttachInfo{};
                info.struct_size = sizeof info;
                info.access_mode = static_cast<uint32_t>(accessModeToPlugin(context.accessMode));
                info.grain_count = context.grainCount;
                info.logical_payload_size = context.logicalPayloadSize;
                auto const flowDir = context.flowDir.string();
                info.flow_dir = flowDir.c_str();
                info.plugin_json = pluginJson.empty() ? nullptr : pluginJson.c_str();

                auto const status = _api->attach(_handle, &info);
                if (status != MXL_STATUS_OK)
                {
                    throw std::runtime_error{"Payload plugin \"" + _backend + "\" attach failed."};
                }
            }

            [[nodiscard]]
            mxlStatus viewAt(std::size_t slotIndex, Grain const* grain, mxlPayloadView* outView) const noexcept override
            {
                if ((grain == nullptr) || (outView == nullptr) || (_handle == nullptr) || (_api->get_view == nullptr))
                {
                    return MXL_ERR_INVALID_ARG;
                }
                auto const status = _api->get_view(_handle, static_cast<uint64_t>(slotIndex), outView);
                if (status != MXL_STATUS_OK)
                {
                    return status;
                }
                outView->grainSize = grain->header.info.grainSize;
                return MXL_STATUS_OK;
            }

        private:
            [[nodiscard]]
            std::string readPluginDescriptor() const
            {
                if (_api->write_plugin_descriptor == nullptr)
                {
                    return "{}";
                }
                auto needed = std::size_t{0};
                auto status = _api->write_plugin_descriptor(_handle, nullptr, &needed);
                if ((status != MXL_STATUS_OK) && (status != MXL_ERR_INVALID_ARG))
                {
                    throw std::runtime_error{"Payload plugin \"" + _backend + "\" failed to size its descriptor."};
                }
                if (needed == 0U)
                {
                    return "{}";
                }
                auto buffer = std::string(needed, '\0');
                status = _api->write_plugin_descriptor(_handle, buffer.data(), &needed);
                if (status != MXL_STATUS_OK)
                {
                    throw std::runtime_error{"Payload plugin \"" + _backend + "\" failed to write its descriptor."};
                }
                if (!buffer.empty() && (buffer.back() == '\0'))
                {
                    buffer.pop_back();
                }
                return buffer.empty() ? std::string{"{}"} : buffer;
            }

            void writeEnvelope(GrainPayloadAttachContext const& context, std::string const& pluginJson) const
            {
                auto pluginValue = picojson::value{};
                auto const err = picojson::parse(pluginValue, pluginJson);
                if (!err.empty() || !pluginValue.is<picojson::object>())
                {
                    throw std::runtime_error{"Payload plugin \"" + _backend + "\" descriptor must be a JSON object."};
                }

                picojson::object root;
                root["version"] = picojson::value{1.0};
                root["backend"] = picojson::value{_backend};
                root["location"] = picojson::value{std::string{_location == MXL_PAYLOAD_LOCATION_DEVICE_MEMORY ? "device" : "host"}};
                root["deviceIndex"] = picojson::value{static_cast<double>(_deviceIndex)};
                root["grainCount"] = picojson::value{static_cast<double>(context.grainCount)};
                root["grainSize"] = picojson::value{static_cast<double>(context.logicalPayloadSize)};
                root["export"] = picojson::value{std::string{"plugin"}};
                root["plugin"] = pluginValue;

                auto const path = makePayloadDescriptorFilePath(context.flowDir);
                auto out = std::ofstream{path, std::ios::trunc};
                if (!out)
                {
                    throw std::filesystem::filesystem_error{"Failed to write payload.json.", path, std::make_error_code(std::errc::io_error)};
                }
                out << picojson::value{root}.serialize(true);
            }

            [[nodiscard]]
            static std::string readExistingPluginObject(std::filesystem::path const& flowDir)
            {
                auto const path = makePayloadDescriptorFilePath(flowDir);
                auto in = std::ifstream{path};
                if (!in)
                {
                    return {};
                }
                auto root = picojson::value{};
                auto const err = picojson::parse(root, in);
                if (!err.empty() || !root.is<picojson::object>())
                {
                    throw std::invalid_argument{"Invalid payload.json: " + err};
                }
                auto const& object = root.get<picojson::object>();
                if (auto it = object.find("plugin"); (it != object.end()) && it->second.is<picojson::object>())
                {
                    return it->second.serialize();
                }
                return {};
            }

            mxlPayloadBackendApiV1 const* _api;
            mxlPayloadBackendHandle _handle{nullptr};
            mxlPayloadLocation _location;
            int32_t _deviceIndex;
            std::string _backend;
        };

        struct LoadedPlugin
        {
            mxlPayloadBackendApiV1 const* api{nullptr};
        };

        struct RegistryState
        {
            std::mutex mutex;
            std::unordered_map<std::string, LoadedPlugin> plugins;
            std::unordered_set<std::string> loadedPaths;
            std::vector<void*> handles;
            bool environmentLoaded{false};
        };

        RegistryState& registryState()
        {
            static RegistryState state;
            return state;
        }
    }

    PayloadBackendRegistry& PayloadBackendRegistry::instance()
    {
        static PayloadBackendRegistry registry;
        return registry;
    }

    void PayloadBackendRegistry::loadLibrary(std::filesystem::path const& path)
    {
        auto& state = registryState();
        auto const lock = std::lock_guard{state.mutex};

        auto const canonical = std::filesystem::weakly_canonical(path);
        auto const key = canonical.string();
        if (state.loadedPaths.contains(key))
        {
            return;
        }
        if (!std::filesystem::is_regular_file(canonical))
        {
            throw std::runtime_error{"Payload plugin not found: " + key};
        }

        auto* handle = ::dlopen(key.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
        {
            auto const* error = ::dlerror();
            throw std::runtime_error{std::string{"dlopen failed for payload plugin: "} + (error != nullptr ? error : key)};
        }

        ::dlerror();
        auto* symbol = ::dlsym(handle, "mxlGetPayloadPluginApi");
        if (symbol == nullptr)
        {
            auto const* error = ::dlerror();
            ::dlclose(handle);
            throw std::runtime_error{std::string{"Payload plugin is missing mxlGetPayloadPluginApi: "} + (error != nullptr ? error : key)};
        }

        auto* entry = reinterpret_cast<mxlGetPayloadPluginApiFn>(symbol);
        mxlPayloadBackendApiV1 const* api = nullptr;
        auto const status = entry(MXL_PAYLOAD_PLUGIN_ABI_VERSION, &api);
        if ((status != MXL_STATUS_OK) || (api == nullptr))
        {
            ::dlclose(handle);
            throw std::runtime_error{"Payload plugin rejected ABI version " + std::to_string(MXL_PAYLOAD_PLUGIN_ABI_VERSION) + ": " + key};
        }
        if ((api->struct_size < sizeof(mxlPayloadBackendApiV1)) || (api->abi_version != MXL_PAYLOAD_PLUGIN_ABI_VERSION) ||
            (api->backend_name == nullptr) || (api->backend_name[0] == '\0') || (api->create == nullptr) || (api->destroy == nullptr) ||
            (api->attach == nullptr) || (api->get_view == nullptr) || (api->mapped_payload_bytes == nullptr))
        {
            ::dlclose(handle);
            throw std::runtime_error{"Payload plugin returned an incomplete API table: " + key};
        }

        auto const name = std::string{api->backend_name};
        if (isBuiltinBackendName(name) || state.plugins.contains(name))
        {
            ::dlclose(handle);
            throw std::runtime_error{"Payload plugin backend name \"" + name + "\" is already registered."};
        }

        state.plugins.emplace(name, LoadedPlugin{.api = api});
        state.handles.push_back(handle);
        state.loadedPaths.insert(key);
        MXL_INFO("Loaded payload plugin \"{}\" from {}", name, key);
    }

    void PayloadBackendRegistry::loadFromEnvironment()
    {
        auto paths = std::vector<std::string>{};
        {
            auto& state = registryState();
            auto const lock = std::lock_guard{state.mutex};
            if (state.environmentLoaded)
            {
                return;
            }
            state.environmentLoaded = true;
            auto const* raw = std::getenv("MXL_PAYLOAD_PLUGIN_PATH");
            if ((raw == nullptr) || (raw[0] == '\0'))
            {
                return;
            }
            auto remaining = std::string{raw};
            while (!remaining.empty())
            {
                auto const split = remaining.find(':');
                auto piece = remaining.substr(0, split);
                if (split == std::string::npos)
                {
                    remaining.clear();
                }
                else
                {
                    remaining.erase(0, split + 1);
                }
                if (!piece.empty())
                {
                    paths.push_back(std::move(piece));
                }
            }
        }
        for (auto const& path : paths)
        {
            loadLibrary(path);
        }
    }

    void PayloadBackendRegistry::loadFromInstanceOptions(std::string const& optionsJson)
    {
        if (optionsJson.empty())
        {
            return;
        }
        auto root = picojson::value{};
        auto const err = picojson::parse(root, optionsJson);
        if (!err.empty() || !root.is<picojson::object>())
        {
            return;
        }
        auto const& object = root.get<picojson::object>();
        auto it = object.find("payloadPlugins");
        if (it == object.end())
        {
            return;
        }
        if (!it->second.is<picojson::array>())
        {
            throw std::invalid_argument{"payloadPlugins must be an array of shared-library paths."};
        }
        for (auto const& entry : it->second.get<picojson::array>())
        {
            if (!entry.is<std::string>() || entry.get<std::string>().empty())
            {
                throw std::invalid_argument{"payloadPlugins entries must be non-empty strings."};
            }
            loadLibrary(entry.get<std::string>());
        }
    }

    void PayloadBackendRegistry::loadCudaLinearPlugin()
    {
        if (isPluginBackend(PAYLOAD_BACKEND_CUDA_LINEAR))
        {
            return;
        }
        loadFromEnvironment();
        if (isPluginBackend(PAYLOAD_BACKEND_CUDA_LINEAR))
        {
            return;
        }

        auto searched = std::string{};
        Dl_info info{};
        if ((::dladdr(reinterpret_cast<void const*>(mxlCreateInstance), &info) != 0) && (info.dli_fname != nullptr))
        {
            auto const candidate = std::filesystem::path{info.dli_fname}.parent_path() / "libmxl-payload-cuda.so";
            searched = candidate.string();
            if (std::filesystem::is_regular_file(candidate))
            {
                loadLibrary(candidate);
                return;
            }
        }
        throw std::runtime_error{"cuda-linear payload plugin libmxl-payload-cuda.so was not found next to libmxl" +
                                 (searched.empty() ? std::string{} : " (looked at " + searched + ")") + "."};
    }

    bool PayloadBackendRegistry::isPluginBackend(std::string const& backendName) const
    {
        auto& state = registryState();
        auto const lock = std::lock_guard{state.mutex};
        return state.plugins.contains(backendName);
    }

    std::unique_ptr<GrainPayloadAllocator> PayloadBackendRegistry::createPlugin(GrainPayloadAllocatorSpec const& spec)
    {
        auto& state = registryState();
        mxlPayloadBackendApiV1 const* api = nullptr;
        {
            auto const lock = std::lock_guard{state.mutex};
            auto it = state.plugins.find(spec.backend);
            if (it == state.plugins.end())
            {
                throw std::invalid_argument{"Unknown payload plugin backend \"" + spec.backend + "\"."};
            }
            api = it->second.api;
        }
        return std::make_unique<PluginPayloadAllocator>(api, spec);
    }
}
