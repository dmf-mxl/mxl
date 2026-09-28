// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cuda_runtime_api.h>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>
#include "CudaTestUtils.hpp"

namespace
{
    /** Id of the flow in data/v210_flow.json. */
    constexpr auto V210_FLOW_ID = "5fbec3b1-1b0f-417d-9059-8b94a47197ed";

    /**
     * \param[in] path A file.
     * \return Its contents.
     */
    std::string readFile(std::filesystem::path const& path)
    {
        auto in = std::ifstream{path};
        auto buffer = std::stringstream{};
        buffer << in.rdbuf();
        return buffer.str();
    }

    /**
     * A temporary MXL domain in shared memory, removed when it goes out of scope.
     */
    class Domain
    {
    public:
        /** Create the domain directory. */
        Domain()
            : _path{}
        {
            auto name = std::string{"/dev/shm/mxl_cuda_test_XXXXXX"};
            REQUIRE(::mkdtemp(name.data()) != nullptr);
            _path = name;
        }

        /** Remove the domain directory. */
        ~Domain()
        {
            auto ec = std::error_code{};
            std::filesystem::remove_all(_path, ec);
        }

        Domain(Domain const&) = delete;
        Domain(Domain&&) = delete;
        Domain& operator=(Domain const&) = delete;
        Domain& operator=(Domain&&) = delete;

        /** \return The domain directory. */
        [[nodiscard]]
        std::filesystem::path const& path() const noexcept
        {
            return _path;
        }

    private:
        std::filesystem::path _path; ///< The domain directory.
    };

    /**
     * \param[in] uuid A device UUID.
     * \return Writer options that put the payload of a flow on that device.
     */
    std::string deviceOptions(std::string const& uuid)
    {
        return R"({"payload": {"backend": "cuda-linear", "deviceUuid": ")" + uuid + R"("}})";
    }

    /**
     * An MXL instance and a writer for the v210 test flow, released on destruction.
     */
    class CudaWriter
    {
    public:
        /**
         * \param[in] domain The MXL domain.
         * \param[in] options The writer options.
         */
        CudaWriter(std::filesystem::path const& domain, std::string const& options)
            : _instance{mxlCreateInstance(domain.string().c_str(), "")}
            , _writer{nullptr}
            , _configInfo{}
        {
            REQUIRE(_instance != nullptr);
            auto const flowDef = readFile(std::filesystem::path{MXL_TEST_DATA_DIR} / "v210_flow.json");
            REQUIRE(mxlCreateFlowWriter(_instance, flowDef.c_str(), options.c_str(), &_writer, &_configInfo, nullptr) == MXL_STATUS_OK);
        }

        /** Release the writer and the instance. */
        ~CudaWriter()
        {
            release();
            mxlDestroyInstance(_instance);
        }

        CudaWriter(CudaWriter const&) = delete;
        CudaWriter(CudaWriter&&) = delete;
        CudaWriter& operator=(CudaWriter const&) = delete;
        CudaWriter& operator=(CudaWriter&&) = delete;

        /** Release the writer before the instance, which deletes the flow. */
        void release()
        {
            if (_writer != nullptr)
            {
                mxlReleaseFlowWriter(_instance, _writer);
                _writer = nullptr;
            }
        }

        /** \return The writer. */
        [[nodiscard]]
        mxlFlowWriter writer() const noexcept
        {
            return _writer;
        }

        /** \return The configuration of the created flow. */
        [[nodiscard]]
        mxlFlowConfigInfo const& configInfo() const noexcept
        {
            return _configInfo;
        }

        /**
         * Write one grain whose payload bytes all have one value, and commit it.
         * \param[in] slots The writer's mapped slots.
         * \param[in] value The byte value.
         * \return The index of the grain.
         */
        std::uint64_t writeGrain(std::vector<mxlGrainStorage> const& slots, std::uint8_t value)
        {
            auto const rate = _configInfo.common.grainRate;
            auto const index = mxlGetCurrentIndex(&rate);
            auto grainInfo = mxlGrainInfo{};
            auto slot = std::uint32_t{0};
            REQUIRE(mxlFlowWriterOpenGrainSlot(_writer, index, &grainInfo, &slot) == MXL_STATUS_OK);
            REQUIRE(::cudaMemset(slots[slot].cuda.pointer, value, grainInfo.grainSize) == cudaSuccess);
            // Device work that writes a grain must be complete before the commit.
            REQUIRE(::cudaDeviceSynchronize() == cudaSuccess);
            grainInfo.validSlices = grainInfo.totalSlices;
            REQUIRE(mxlFlowWriterCommitGrain(_writer, &grainInfo) == MXL_STATUS_OK);
            return index;
        }

    private:
        mxlInstance _instance;         ///< The writer's instance.
        mxlFlowWriter _writer;         ///< The writer.
        mxlFlowConfigInfo _configInfo; ///< Configuration of the created flow.
    };

    /**
     * Copy a grain from device memory and check that every byte has one value.
     * \param[in] pointer The device address of the grain.
     * \param[in] size The payload size.
     * \param[in] value The expected byte value.
     */
    void requireDeviceBytes(void const* pointer, std::size_t size, std::uint8_t value)
    {
        auto bytes = std::vector<std::uint8_t>(size);
        REQUIRE(::cudaMemcpy(bytes.data(), pointer, size, cudaMemcpyDeviceToHost) == cudaSuccess);
        for (auto const byte : bytes)
        {
            REQUIRE(byte == value);
        }
    }

    /**
     * Run the reader helper in another process.
     * \param[in] args The arguments after the program name.
     * \return The exit status of the helper, or -1 if it could not be run.
     */
    int runReaderHelper(std::vector<std::string> const& args)
    {
        auto argv = std::vector<char*>{};
        auto program = std::string{MXL_CUDA_READER_HELPER};
        argv.push_back(program.data());
        auto copies = args;
        for (auto& arg : copies)
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        auto pid = pid_t{};
        if (::posix_spawn(&pid, program.c_str(), nullptr, nullptr, argv.data(), environ) != 0)
        {
            return -1;
        }
        auto status = 0;
        if (::waitpid(pid, &status, 0) != pid)
        {
            return -1;
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
}

TEST_CASE("CUDA payload : Writer and reader in one process", "[cuda payload]")
{
    if (mxl::tests::cuda::deviceCount() == 0)
    {
        SKIP("No CUDA device.");
    }
    auto const uuid = mxl::tests::cuda::deviceUuid(0);
    auto const domain = Domain{};
    auto writer = CudaWriter{domain.path(), deviceOptions(uuid)};

    // The flow records the device the backend chose. The deprecated payload location stays at 0, and the storage
    // layout says where the payload is.
    REQUIRE(writer.configInfo().common.payloadLocation == 0U);
    REQUIRE(writer.configInfo().common.deviceIndex == 0);

    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowWriterGetStorageLayout(writer.writer(), &layout) == MXL_STATUS_OK);
    REQUIRE(layout.storageType == MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER);
    REQUIRE(layout.deviceIndex == 0);
    auto properties = cudaDeviceProp{};
    REQUIRE(::cudaGetDeviceProperties(&properties, 0) == cudaSuccess);
    REQUIRE(std::memcmp(layout.deviceUuid, properties.uuid.bytes, sizeof(layout.deviceUuid)) == 0);

    // Slots are device memory, 256-byte aligned.
    auto writerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowWriterMapSlots(writer.writer(), layout.slotCount, writerSlots.data()) == MXL_STATUS_OK);
    for (auto const& slot : writerSlots)
    {
        REQUIRE(slot.storageType == MXL_PAYLOAD_STORAGE_CUDA_DEVICE_POINTER);
        REQUIRE((reinterpret_cast<std::uintptr_t>(slot.cuda.pointer) % 256U) == 0U);
        auto attributes = cudaPointerAttributes{};
        REQUIRE(::cudaPointerGetAttributes(&attributes, slot.cuda.pointer) == cudaSuccess);
        REQUIRE(attributes.type == cudaMemoryTypeDevice);
    }

    // The pointer based API refuses device memory.
    auto grainInfo = mxlGrainInfo{};
    std::uint8_t* payload = nullptr;
    auto const rate = writer.configInfo().common.grainRate;
    REQUIRE(mxlFlowWriterOpenGrain(writer.writer(), mxlGetCurrentIndex(&rate), &grainInfo, &payload) == MXL_ERR_UNSUPPORTED_OPERATION);

    auto const index = writer.writeGrain(writerSlots, 0xAB);

    // A reader in the same process shares the writer's allocation.
    auto readerInstance = mxlCreateInstance(domain.path().string().c_str(), "");
    REQUIRE(readerInstance != nullptr);
    mxlFlowReader reader = nullptr;
    REQUIRE(mxlCreateFlowReader(readerInstance, V210_FLOW_ID, "", &reader) == MXL_STATUS_OK);
    auto readerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowReaderMapSlots(reader, layout.slotCount, readerSlots.data()) == MXL_STATUS_OK);

    auto slot = std::uint32_t{0};
    REQUIRE(mxlFlowReaderGetGrainSlot(reader, index, MXL_GRAIN_VALID_SLICES_ALL, 0U, &grainInfo, &slot) == MXL_STATUS_OK);
    REQUIRE(readerSlots[slot].cuda.pointer == writerSlots[slot].cuda.pointer);
    requireDeviceBytes(readerSlots[slot].cuda.pointer, grainInfo.grainSize, 0xAB);

    // The pointer based API refuses device memory for readers too.
    REQUIRE(mxlFlowReaderGetGrain(reader, index, 0U, &grainInfo, &payload) == MXL_ERR_UNSUPPORTED_OPERATION);

    // The allocation stays valid for the reader after the writer is released.
    writer.release();
    requireDeviceBytes(readerSlots[slot].cuda.pointer, grainInfo.grainSize, 0xAB);

    REQUIRE(mxlReleaseFlowReader(readerInstance, reader) == MXL_STATUS_OK);
    mxlDestroyInstance(readerInstance);
}

TEST_CASE("CUDA payload : Reader in another process", "[cuda payload]")
{
    if (mxl::tests::cuda::deviceCount() == 0)
    {
        SKIP("No CUDA device.");
    }
    auto const uuid = mxl::tests::cuda::deviceUuid(0);
    auto const domain = Domain{};
    auto writer = CudaWriter{domain.path(), deviceOptions(uuid)};

    auto layout = mxlGrainStorageLayout{};
    REQUIRE(mxlFlowWriterGetStorageLayout(writer.writer(), &layout) == MXL_STATUS_OK);
    auto writerSlots = std::vector<mxlGrainStorage>(layout.slotCount);
    REQUIRE(mxlFlowWriterMapSlots(writer.writer(), layout.slotCount, writerSlots.data()) == MXL_STATUS_OK);
    auto const index = writer.writeGrain(writerSlots, 0x5C);

    // The helper imports the allocation with CUDA IPC.
    REQUIRE(runReaderHelper({domain.path().string(), V210_FLOW_ID, std::to_string(index), std::to_string(0x5C)}) == 0);

    // A reader can name the device on which it accesses the payload.
    REQUIRE(runReaderHelper({domain.path().string(),
                V210_FLOW_ID,
                std::to_string(index),
                std::to_string(0x5C),
                R"({"payload": {"deviceUuid": ")" + uuid + R"("}})"}) == 0);
}

TEST_CASE("CUDA payload : Descriptors in an unknown format are refused", "[cuda payload]")
{
    if (mxl::tests::cuda::deviceCount() == 0)
    {
        SKIP("No CUDA device.");
    }
    auto const domain = Domain{};
    auto writer = CudaWriter{domain.path(), deviceOptions(mxl::tests::cuda::deviceUuid(0))};

    // A writer running another build of the backend could write payload.cuda.json in another format.
    auto const descriptorPath = domain.path() / (std::string{V210_FLOW_ID} + ".mxl-flow") / "payload.cuda.json";
    auto descriptor = readFile(descriptorPath);
    auto const pos = descriptor.find("\"version\": 1");
    REQUIRE(pos != std::string::npos);
    descriptor.replace(pos, std::strlen("\"version\": 1"), "\"version\": 2");
    {
        auto out = std::ofstream{descriptorPath, std::ios::out | std::ios::trunc};
        out << descriptor;
    }

    auto instance = mxlCreateInstance(domain.path().string().c_str(), "");
    REQUIRE(instance != nullptr);
    mxlFlowReader reader = nullptr;
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, "", &reader) == MXL_STATUS_OK);
    auto const grainCount = writer.configInfo().discrete.grainCount;
    auto slots = std::vector<mxlGrainStorage>(grainCount);
    REQUIRE(mxlFlowReaderMapSlots(reader, grainCount, slots.data()) == MXL_ERR_UNSUPPORTED_OPERATION);

    REQUIRE(mxlReleaseFlowReader(instance, reader) == MXL_STATUS_OK);
    mxlDestroyInstance(instance);
}

TEST_CASE("CUDA payload : Invalid options", "[cuda payload]")
{
    if (mxl::tests::cuda::deviceCount() == 0)
    {
        SKIP("No CUDA device.");
    }
    auto const uuid = mxl::tests::cuda::deviceUuid(0);
    auto const domain = Domain{};
    auto instance = mxlCreateInstance(domain.path().string().c_str(), "");
    REQUIRE(instance != nullptr);
    auto const flowDef = readFile(std::filesystem::path{MXL_TEST_DATA_DIR} / "v210_flow.json");

    auto const dataFlowDef = readFile(std::filesystem::path{MXL_TEST_DATA_DIR} / "data_flow.json");

    auto const tryCreateFlow = [&](std::string const& definition, std::string const& options)
    {
        mxlFlowWriter writer = nullptr;
        auto const status = mxlCreateFlowWriter(instance, definition.c_str(), options.c_str(), &writer, nullptr, nullptr);
        if (status == MXL_STATUS_OK)
        {
            mxlReleaseFlowWriter(instance, writer);
        }
        return status;
    };
    auto const tryCreate = [&](std::string const& options)
    {
        return tryCreateFlow(flowDef, options);
    };

    // A writer must name the backend. Without it, the options go to the built-in host storage, which accepts none.
    REQUIRE(tryCreate(R"({"payload": {"deviceUuid": ")" + uuid + R"("}})") == MXL_ERR_INVALID_ARG);

    // A writer must name its device by UUID. Device indices are not accepted.
    REQUIRE(tryCreate(R"({"payload": {"backend": "cuda-linear"}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreate(R"({"payload": {"backend": "cuda-linear", "deviceIndex": 0}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreate(R"({"payload": {"backend": "cuda-linear", "deviceUuid": "GPU-not-a-uuid"}})") == MXL_ERR_INVALID_ARG);
    REQUIRE(tryCreate(R"({"payload": {"backend": "cuda-linear", "deviceUuid": 0}})") == MXL_ERR_INVALID_ARG);

    // The backend holds video flows only.
    REQUIRE(tryCreateFlow(dataFlowDef, deviceOptions(uuid)) == MXL_ERR_UNSUPPORTED_OPERATION);

    // A well formed UUID of a device that is not visible.
    REQUIRE(tryCreate(deviceOptions("GPU-00000000-0000-0000-0000-000000000000")) == MXL_ERR_NOT_FOUND);

    // A UUID without the prefix and hyphens is accepted.
    auto compact = uuid.substr(4);
    std::erase(compact, '-');
    REQUIRE(tryCreate(deviceOptions(compact)) == MXL_STATUS_OK);

    // Reader options are validated by the backend.
    auto writer = CudaWriter{domain.path(), deviceOptions(uuid)};
    mxlFlowReader reader = nullptr;
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, R"({"payload": {"unknown": 1}})", &reader) == MXL_ERR_INVALID_ARG);
    REQUIRE(mxlCreateFlowReader(instance, V210_FLOW_ID, R"({"payload": {"deviceUuid": "zz"}})", &reader) == MXL_ERR_INVALID_ARG);

    mxlDestroyInstance(instance);
}
