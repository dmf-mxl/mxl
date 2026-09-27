// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include "GrainPayloadAllocator.hpp"

namespace mxl::lib
{
    /**
     * Process-wide map from backend name to either a built-in allocator or a
     * dlopen'd C ABI plugin. Built-in names cannot be overridden.
     */
    class PayloadBackendRegistry
    {
    public:
        static PayloadBackendRegistry& instance();

        void loadLibrary(std::filesystem::path const& path);
        void loadFromEnvironment();
        void loadFromInstanceOptions(std::string const& optionsJson);

        /**
         * dlopen libmxl-payload-cuda.so from the same directory as libmxl when
         * the cuda-linear backend is not already registered.
         */
        void loadCudaLinearPlugin();

        [[nodiscard]]
        bool isPluginBackend(std::string const& backendName) const;

        [[nodiscard]]
        std::unique_ptr<GrainPayloadAllocator> createPlugin(GrainPayloadAllocatorSpec const& spec);

    private:
        PayloadBackendRegistry() = default;
    };
}
