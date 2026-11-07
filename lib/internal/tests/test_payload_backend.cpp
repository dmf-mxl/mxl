// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <mxl/flow.h>
#include "mxl-internal/PayloadBackend.hpp"
#include "../../tests/Utils.hpp"

using namespace mxl::lib;
namespace fs = std::filesystem;

namespace
{
    /**
     * \return A loader that searches the directory where the build puts the test backends.
     */
    PayloadBackendLoader& testLoader()
    {
        static auto loader = PayloadBackendLoader{std::vector{fs::path{MXL_TEST_PAYLOAD_BACKEND_DIR}}};
        return loader;
    }

    /**
     * \param[in] accessMode MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY or MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE.
     * \return Flow geometry with 4 slots of 1000 bytes each: one plane of 100 slices of 10 bytes.
     */
    mxlPayloadBackendFlowInfo testFlowInfo(std::uint32_t accessMode)
    {
        return mxlPayloadBackendFlowInfo{
            .structSize = sizeof(mxlPayloadBackendFlowInfo),
            .grainCount = 4U,
            .grainPayloadSize = 1000U,
            .accessMode = accessMode,
            .planeCount = 1U,
            .slicesPerGrain = 100U,
            .sliceSizes = {10U, 0U, 0U, 0U},
            .format = MXL_DATA_FORMAT_VIDEO,
        };
    }

    /**
     * \return The discrete configuration that matches testFlowInfo().
     */
    mxlDiscreteFlowConfigInfo testDiscreteConfig()
    {
        auto config = mxlDiscreteFlowConfigInfo{};
        config.sliceSizes[0] = 10U;
        config.grainCount = 4U;
        return config;
    }

    /**
     * Payload storage whose layout description can be replaced by a test. Holds no memory.
     */
    class FakeLayoutStorage final : public PayloadStorage
    {
    public:
        /** Function that changes the default layout. */
        using LayoutEditor = void (*)(mxlGrainStorageLayout&);

        /**
         * \param[in] editor Called by describeLayout() after the required fields are set.
         */
        explicit FakeLayoutStorage(LayoutEditor editor) noexcept
            : _editor{editor}
        {}

        /** \see PayloadStorage::type() */
        [[nodiscard]]
        mxlPayloadStorageType type() const noexcept override
        {
            return MXL_PAYLOAD_STORAGE_HOST_POINTER;
        }

        /** \see PayloadStorage::deviceIndex() */
        [[nodiscard]]
        std::int32_t deviceIndex() const noexcept override
        {
            return -1;
        }

        /** \see PayloadStorage::slotCount() */
        [[nodiscard]]
        std::size_t slotCount() const noexcept override
        {
            return 4U;
        }

        /** Not used by these tests. */
        [[nodiscard]]
        mxlGrainStorage slotStorage(std::size_t) const override
        {
            throw std::logic_error{"Not implemented."};
        }

        /** Not used by these tests. */
        void mapSlots() const override
        {}

        /** \see PayloadStorage::describeLayout() */
        void describeLayout(mxlGrainStorageLayout& layout) const override
        {
            layout.storageType = type();
            layout.deviceIndex = deviceIndex();
            _editor(layout);
        }

    private:
        /** Changes the default layout. */
        LayoutEditor _editor;
    };

    /**
     * Removes a directory tree when it goes out of scope.
     */
    struct TempDirectory
    {
        /** Create a unique directory in the test domain location. */
        TempDirectory()
            : path{mxl::tests::makeTempDomain()}
        {}

        /** Remove the directory and everything below it. */
        ~TempDirectory()
        {
            auto ec = std::error_code{};
            fs::remove_all(path, ec);
        }

        TempDirectory(TempDirectory const&) = delete;
        TempDirectory(TempDirectory&&) = delete;
        TempDirectory& operator=(TempDirectory const&) = delete;
        TempDirectory& operator=(TempDirectory&&) = delete;

        fs::path path; ///< The directory.
    };
}

TEST_CASE("Payload backend loader: backend names", "[payload backend]")
{
    REQUIRE(PayloadBackendLoader::isValidBackendName("cuda-linear"));
    REQUIRE(PayloadBackendLoader::isValidBackendName("test-file"));
    REQUIRE(PayloadBackendLoader::isValidBackendName("a"));
    REQUIRE(PayloadBackendLoader::isValidBackendName("0abc"));

    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName(""));
    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName("-leading-hyphen"));
    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName("Upper"));
    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName("../escape"));
    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName("with/slash"));
    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName("with.dot"));
    REQUIRE_FALSE(PayloadBackendLoader::isValidBackendName(std::string(65, 'a')));

    REQUIRE(PayloadBackendLoader::libraryFileName("cuda-linear") == "libmxl-payload-cuda-linear.so");
}

TEST_CASE("Payload backend loader: default loader searches the directory of its own binary last", "[payload backend]")
{
    // In this test binary the loader code is linked into the executable itself, not into libmxl.
    auto const& directories = PayloadBackendLoader::defaultLoader().searchDirectories();
    REQUIRE_FALSE(directories.empty());
#ifdef __linux__
    REQUIRE(directories.back() == fs::canonical("/proc/self/exe").parent_path());
#endif
}

TEST_CASE("Payload backend loader: plugin path", "[payload backend]")
{
    REQUIRE(PayloadBackendLoader::parsePluginPath("").empty());
    REQUIRE(PayloadBackendLoader::parsePluginPath("::").empty());
    REQUIRE(PayloadBackendLoader::parsePluginPath("/opt/mxl") == std::vector<fs::path>{"/opt/mxl"});

    // Empty and relative entries are skipped, and the order is kept.
    REQUIRE(PayloadBackendLoader::parsePluginPath("/b:relative::/a:") == std::vector<fs::path>{"/b", "/a"});
}

TEST_CASE("Payload backend loader: the first directory that holds the library is used", "[payload backend]")
{
    auto const empty = TempDirectory{};
    auto const shadow = TempDirectory{};
    auto const testDirectory = fs::path{MXL_TEST_PAYLOAD_BACKEND_DIR};

    // Directories without the library are skipped.
    auto loader = PayloadBackendLoader{
        std::vector{empty.path, testDirectory}
    };
    REQUIRE(std::strcmp(loader.load("test-file").name, "test-file") == 0);

    // An invalid library found first is an error. The loader does not fall back to a later directory.
    fs::copy_file(testDirectory / PayloadBackendLoader::libraryFileName("test-no-entry"),
        shadow.path / PayloadBackendLoader::libraryFileName("test-file"));
    auto shadowedLoader = PayloadBackendLoader{
        std::vector{shadow.path, testDirectory}
    };
    REQUIRE_THROWS_AS(shadowedLoader.load("test-file"), std::runtime_error);
}

TEST_CASE("Payload backend loader: rejects invalid backends", "[payload backend]")
{
    auto& loader = testLoader();

    REQUIRE_THROWS_AS(loader.load("../escape"), std::invalid_argument);
    REQUIRE_THROWS_AS(loader.load("host"), std::invalid_argument);
    REQUIRE_THROWS_AS(loader.load("does-not-exist"), std::runtime_error);
    REQUIRE_THROWS_AS(loader.load("test-no-entry"), std::runtime_error);
    REQUIRE_THROWS_AS(loader.load("test-bad-name"), std::runtime_error);

    // A loader without search directories fails with a clear error.
    auto emptyLoader = PayloadBackendLoader{std::vector<fs::path>{}};
    REQUIRE_THROWS_AS(emptyLoader.load("test-file"), std::runtime_error);
}

TEST_CASE("Payload backend loader: loads a valid backend once", "[payload backend]")
{
    auto& loader = testLoader();

    auto const& api = loader.load("test-file");
    REQUIRE(api.abiVersion == MXL_PAYLOAD_BACKEND_ABI_VERSION);
    REQUIRE(std::strcmp(api.name, "test-file") == 0);
    REQUIRE(api.storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER);

    // A second load returns the same table.
    REQUIRE(&loader.load("test-file") == &api);
}

TEST_CASE("Plugin payload storage: prepare, publish and open", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};
    auto const stagingDir = domain.path / ".mxl-tmp-staging";
    auto const finalDir = domain.path / "flow.mxl-flow";
    fs::create_directory(stagingDir);

    // The writer prepares the storage while the flow is still in its temporary directory.
    auto writerStorage = PluginPayloadStorage::prepare(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE), "{}", stagingDir, finalDir);
    REQUIRE(writerStorage->type() == MXL_PAYLOAD_STORAGE_HOST_POINTER);
    REQUIRE(writerStorage->deviceIndex() == -1);
    REQUIRE(writerStorage->slotCount() == 4U);
    REQUIRE(fs::exists(stagingDir / "payload.test"));

    // Slots are 256 byte aligned and do not overlap.
    auto const slot0 = writerStorage->slotStorage(0U);
    auto const slot1 = writerStorage->slotStorage(1U);
    REQUIRE(slot0.version == 1U);
    REQUIRE(slot0.size == sizeof(mxlGrainStorage));
    REQUIRE(slot0.slot == 0U);
    REQUIRE(slot1.slot == 1U);
    REQUIRE((static_cast<std::uint8_t*>(slot1.host.pointer) - static_cast<std::uint8_t*>(slot0.host.pointer)) == 1024);

    auto* const writerSlot2 = static_cast<std::uint8_t*>(writerStorage->slotStorage(2U).host.pointer);
    writerSlot2[0] = 0x5A;
    writerSlot2[999] = 0xA5;

    // Publishing renames the directory. The writer's mapping stays valid.
    fs::rename(stagingDir, finalDir);
    REQUIRE(writerSlot2[0] == 0x5A);

    // A reader opens the published flow, maps all slots up front, and sees the same bytes through its own mapping.
    auto readerStorage = PluginPayloadStorage::open(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY), "{}", finalDir);
    readerStorage->mapSlots();
    readerStorage->mapSlots(); // A second call does nothing.
    auto const readerSlot2 = readerStorage->slotStorage(2U);
    auto const* const readerBytes = static_cast<std::uint8_t const*>(readerSlot2.host.pointer);
    REQUIRE(readerBytes != writerSlot2);
    REQUIRE(readerBytes[0] == 0x5A);
    REQUIRE(readerBytes[999] == 0xA5);

    // Slot descriptions do not change.
    REQUIRE(readerStorage->slotStorage(2U).host.pointer == readerSlot2.host.pointer);

    REQUIRE_THROWS_AS(readerStorage->slotStorage(4U), std::out_of_range);
}

TEST_CASE("Plugin payload storage: open does not import the payload", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};

    // There is no payload file. Opening still succeeds, because the backend only imports on first slot access.
    auto storage = PluginPayloadStorage::open(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY), "{}", domain.path);
    REQUIRE(storage->slotCount() == 4U);
    REQUIRE(storage->type() == MXL_PAYLOAD_STORAGE_HOST_POINTER);

    // Mapping fails and reports the backend's status.
    try
    {
        storage->mapSlots();
        FAIL("mapSlots() should have thrown.");
    }
    catch (PayloadStorageError const& e)
    {
        REQUIRE(e.status() == MXL_ERR_NOT_FOUND);
    }

    // Without an explicit mapping, the first slot access maps and fails the same way.
    auto lazyStorage = PluginPayloadStorage::open(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY), "{}", domain.path);
    try
    {
        (void)lazyStorage->slotStorage(0U);
        FAIL("slotStorage() should have thrown.");
    }
    catch (PayloadStorageError const& e)
    {
        REQUIRE(e.status() == MXL_ERR_NOT_FOUND);
    }
}

TEST_CASE("Plugin payload storage: prepare failure is reported", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};
    auto const missingDir = domain.path / "does-not-exist";

    REQUIRE_THROWS_AS(
        PluginPayloadStorage::prepare(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE), "{}", missingDir, domain.path / "final"),
        PayloadStorageError);
}

TEST_CASE("Plugin payload storage: the backend decides which formats it stores", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};

    auto info = testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE);
    info.format = MXL_DATA_FORMAT_AUDIO;
    try
    {
        static_cast<void>(PluginPayloadStorage::prepare(api, info, "{}", domain.path, domain.path / "final"));
        FAIL("prepare accepted a format the backend does not store.");
    }
    catch (PayloadStorageError const& e)
    {
        REQUIRE(e.status() == MXL_ERR_UNSUPPORTED_OPERATION);
    }
}

TEST_CASE("Plugin payload storage: the backend completes the layout", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};

    auto storage = PluginPayloadStorage::open(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY), "{}", domain.path);
    auto const layout = makeGrainStorageLayout(testDiscreteConfig(), 100U, *storage);

    REQUIRE(layout.storageType == MXL_PAYLOAD_STORAGE_HOST_POINTER);
    REQUIRE(layout.slotCount == 4U);
    REQUIRE(layout.planeCount == 1U);
    REQUIRE(layout.planes[0].offset == 0U);
    REQUIRE(layout.planes[0].pitch == 10U);
    REQUIRE(layout.planes[0].size == 1000U);

    // The UUID comes from the backend.
    for (auto i = std::size_t{0}; i < sizeof(layout.deviceUuid); ++i)
    {
        REQUIRE(layout.deviceUuid[i] == static_cast<std::uint8_t>(0x10U + i));
    }
}

TEST_CASE("Payload storage layout: a storage may choose its own plane geometry", "[payload backend]")
{
    // Device allocated buffers often pad each line to an alignment the device chooses. A pitch of 16 bytes for
    // slices of 10 bytes needs 99 * 16 + 10 bytes for 100 slices.
    auto const padded = FakeLayoutStorage{[](mxlGrainStorageLayout& layout)
        {
            layout.planes[0].offset = 4096U;
            layout.planes[0].pitch = 16U;
            layout.planes[0].size = (99U * 16U) + 10U;
        }};
    auto const layout = makeGrainStorageLayout(testDiscreteConfig(), 100U, padded);
    REQUIRE(layout.planes[0].offset == 4096U);
    REQUIRE(layout.planes[0].pitch == 16U);
    REQUIRE(layout.planes[0].size == 1594U);
}

TEST_CASE("Payload storage layout: invalid descriptions are rejected", "[payload backend]")
{
    // A pitch smaller than a slice.
    auto const shortPitch = FakeLayoutStorage{[](mxlGrainStorageLayout& layout)
        {
            layout.planes[0].pitch = 8U;
        }};
    REQUIRE_THROWS_AS(makeGrainStorageLayout(testDiscreteConfig(), 100U, shortPitch), std::logic_error);

    // A plane too small for its slices at the reported pitch.
    auto const shortPlane = FakeLayoutStorage{[](mxlGrainStorageLayout& layout)
        {
            layout.planes[0].pitch = 16U;
            layout.planes[0].size = 1000U;
        }};
    REQUIRE_THROWS_AS(makeGrainStorageLayout(testDiscreteConfig(), 100U, shortPlane), std::logic_error);

    // Fields that belong to libmxl.
    auto const changedSlots = FakeLayoutStorage{[](mxlGrainStorageLayout& layout)
        {
            layout.slotCount = 5U;
        }};
    REQUIRE_THROWS_AS(makeGrainStorageLayout(testDiscreteConfig(), 100U, changedSlots), std::logic_error);

    auto const changedPlanes = FakeLayoutStorage{[](mxlGrainStorageLayout& layout)
        {
            layout.planeCount = 2U;
        }};
    REQUIRE_THROWS_AS(makeGrainStorageLayout(testDiscreteConfig(), 100U, changedPlanes), std::logic_error);
}

TEST_CASE("Plugin payload storage: backend options reach the backend", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};
    auto const stagingDir = domain.path / ".mxl-tmp-staging";
    auto const finalDir = domain.path / "flow.mxl-flow";
    fs::create_directory(stagingDir);

    // The test backend accepts "alignment" from writers. Slots then start 4096 bytes apart.
    auto writerStorage = PluginPayloadStorage::prepare(
        api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE), R"({"alignment": 4096})", stagingDir, finalDir);
    auto const slot0 = writerStorage->slotStorage(0U);
    auto const slot1 = writerStorage->slotStorage(1U);
    REQUIRE((static_cast<std::uint8_t*>(slot1.host.pointer) - static_cast<std::uint8_t*>(slot0.host.pointer)) == 4096);
    static_cast<std::uint8_t*>(slot1.host.pointer)[0] = 0x77;

    // A reader learns the stride from the backend's own file, not from its options.
    fs::rename(stagingDir, finalDir);
    auto readerStorage = PluginPayloadStorage::open(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY), "{}", finalDir);
    readerStorage->mapSlots();
    REQUIRE(static_cast<std::uint8_t const*>(readerStorage->slotStorage(1U).host.pointer)[0] == 0x77);
}

TEST_CASE("Plugin payload storage: the backend rejects invalid options", "[payload backend]")
{
    auto const& api = testLoader().load("test-file");
    auto const domain = TempDirectory{};

    auto const expectInvalid = [](auto&& call)
    {
        try
        {
            (void)call();
            FAIL("The backend should have rejected the options.");
        }
        catch (PayloadStorageError const& e)
        {
            REQUIRE(e.status() == MXL_ERR_INVALID_ARG);
        }
    };

    // Writer options the test backend does not know, or values it does not accept.
    for (auto const* options : {R"({"alignment": 100})", R"({"alignment": "large"})", R"({"deviceIndex": 0})", "not json"})
    {
        CAPTURE(options);
        expectInvalid(
            [&]()
            {
                return PluginPayloadStorage::prepare(
                    api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_WRITE), options, domain.path, domain.path / "final");
            });
    }

    // The test backend accepts no reader options.
    expectInvalid([&]()
        { return PluginPayloadStorage::open(api, testFlowInfo(MXL_PAYLOAD_BACKEND_ACCESS_READ_ONLY), R"({"deviceIndex": 1})", domain.path); });
}
