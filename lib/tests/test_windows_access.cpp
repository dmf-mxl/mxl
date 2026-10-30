// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

/** @file
 * @brief Windows equivalents of the POSIX permission and file corruption tests.
 */

#include <cstddef>
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <aclapi.h>
#include <windows.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "mxl-internal/EventRingBuffer.hpp"
#include "mxl-internal/FlowManager.hpp"
#include "mxl-internal/FlowParser.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/SharedMemory.hpp"
#include "mxl/flow.h"
#include "mxl/mxl.h"
#include "Utils.hpp"

namespace
{
    using namespace mxl::lib;

    /// Closes a Windows handle at scope exit.
    using OwnedHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;
    /// Frees a security descriptor or ACL allocated by the Windows security APIs.
    using LocalMemory = std::unique_ptr<void, decltype(&::LocalFree)>;
    /// Releases an instance and its remaining flow handles at scope exit.
    using OwnedInstance = std::unique_ptr<std::remove_pointer_t<mxlInstance>, decltype(&mxlDestroyInstance)>;

    /**
     * Report a Windows security API failure.
     * @param result Error code returned by the operation.
     * @param operation Description used in the exception.
     * @throws std::system_error If result is not ERROR_SUCCESS.
     */
    void checkWindowsResult(DWORD result, char const* operation)
    {
        if (result != ERROR_SUCCESS)
        {
            throw std::system_error{static_cast<int>(result), std::system_category(), operation};
        }
    }

    /**
     * Deny selected rights on one test file or directory, then restore its original DACL and inheritance setting.
     * The control handle is opened before the denial, so restoration does not need to reopen the restricted file or directory.
     * The deny ACE is not inherited by children. Callers may only pass files and directories owned by their test fixture.
     */
    class ScopedDeniedAccess
    {
    public:
        /**
         * Save the DACL and add an explicit denial for Everyone on the file or directory itself.
         * @param directory Existing file or directory owned by the test fixture.
         * @param rights Specific file or directory rights to deny.
         * @throws std::system_error Opening it, reading its security descriptor or applying the denial fails.
         */
        ScopedDeniedAccess(std::filesystem::path const& directory, DWORD rights)
            : _handle{::CreateFileW(directory.c_str(), READ_CONTROL | WRITE_DAC, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr),
                  &::CloseHandle}
            , _original{nullptr, &::LocalFree}
            , _dacl{nullptr}
            , _restoreFlags{DACL_SECURITY_INFORMATION}
            , _active{false}
        {
            if (_handle.get() == INVALID_HANDLE_VALUE)
            {
                auto const error = ::GetLastError();
                [[maybe_unused]]
                auto const invalidHandle = _handle.release();
                checkWindowsResult(error, "Could not open the test directory for ACL restoration.");
            }
            auto descriptor = PSECURITY_DESCRIPTOR{};
            auto const readResult = ::GetSecurityInfo(
                _handle.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &_dacl, nullptr, &descriptor);
            _original.reset(descriptor);
            checkWindowsResult(readResult, "Could not read the test directory's DACL.");
            auto control = SECURITY_DESCRIPTOR_CONTROL{};
            auto revision = DWORD{};
            if (::GetSecurityDescriptorControl(descriptor, &control, &revision) == FALSE)
            {
                checkWindowsResult(::GetLastError(), "Could not read the DACL inheritance setting.");
            }
            _restoreFlags |= (control & SE_DACL_PROTECTED) != 0 ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;

            alignas(DWORD) auto everyone = std::array<std::byte, SECURITY_MAX_SID_SIZE>{};
            auto sidSize = DWORD{everyone.size()};
            if (::CreateWellKnownSid(WinWorldSid, nullptr, everyone.data(), &sidSize) == FALSE)
            {
                checkWindowsResult(::GetLastError(), "Could not create the Everyone SID.");
            }
            auto denial = EXPLICIT_ACCESSW{};
            denial.grfAccessPermissions = rights;
            denial.grfAccessMode = DENY_ACCESS;
            denial.grfInheritance = NO_INHERITANCE;
            ::BuildTrusteeWithSidW(&denial.Trustee, everyone.data());
            auto deniedAcl = PACL{};
            auto const aclResult = ::SetEntriesInAclW(1, &denial, _dacl, &deniedAcl);
            auto const ownedAcl = LocalMemory{deniedAcl, &::LocalFree};
            checkWindowsResult(aclResult, "Could not build the test DACL.");
            checkWindowsResult(
                ::SetSecurityInfo(_handle.get(),
                    SE_FILE_OBJECT,
                    DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                    nullptr,
                    nullptr,
                    deniedAcl,
                    nullptr),
                "Could not apply the test access denial.");
            _active = true;
        }

        /// Restore access if an assertion exits the test before explicit restoration.
        ~ScopedDeniedAccess()
        {
            static_cast<void>(restore());
        }

        /**
         * Restore the saved DACL and inheritance setting through the control handle. Repeated calls are harmless.
         * @return ERROR_SUCCESS on success, or the Windows security API error code. Failure leaves restoration pending.
         */
        DWORD restore() noexcept
        {
            if (!_active)
            {
                return ERROR_SUCCESS;
            }
            auto const result = ::SetSecurityInfo(_handle.get(), SE_FILE_OBJECT, _restoreFlags, nullptr, nullptr, _dacl, nullptr);
            if (result == ERROR_SUCCESS)
            {
                _active = false;
            }
            return result;
        }

    private:
        OwnedHandle _handle;                ///< Control handle that retains permission to restore the DACL.
        LocalMemory _original;              ///< Original security descriptor, owning the saved DACL.
        PACL _dacl;                         ///< DACL inside _original, including its original inherited ACEs.
        SECURITY_INFORMATION _restoreFlags; ///< Flags that restore the original inheritance setting.
        bool _active;                       ///< Whether the temporary denial still needs restoration.
    };

    /**
     * Create an instance whose resources are released if an assertion fails.
     * @param domain Existing test domain directory.
     * @return Owned public API instance.
     */
    OwnedInstance createInstance(std::filesystem::path const& domain)
    {
        auto instance = OwnedInstance{mxlCreateInstance(domain.string().c_str(), "{}"), &mxlDestroyInstance};
        REQUIRE(instance);
        return instance;
    }
}

/** @test An ACL denial of subdirectory creation makes internal flow creation fail until access is restored. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows permissions: FlowManager rejects an unwritable domain", "[windows][permissions]")
{
    auto manager = FlowManager{domain};
    auto const definition = mxl::tests::readFile("data/audio_flow.json");
    auto const id = FlowParser{definition}.getId();
    auto const rate = mxlRational{.numerator = 48000, .denominator = 1};
    auto const createWithDeniedAccess = [&]
    {
        auto const denied = ScopedDeniedAccess{domain, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY};
        manager.createOrOpenContinuousFlow(id, definition, MXL_DATA_FORMAT_AUDIO, rate, 2, sizeof(float), 128);
    };
    REQUIRE_THROWS_AS(createWithDeniedAccess(), std::filesystem::filesystem_error);
    // The exception must also restore access before another creation attempt.
    auto [created, flow] = manager.createOrOpenContinuousFlow(id, definition, MXL_DATA_FORMAT_AUDIO, rate, 2, sizeof(float), 128);
    REQUIRE(created);
    REQUIRE(manager.deleteFlow(std::move(flow)));
}

/** @test The public writer API reports permission denied for an ACL-protected domain and succeeds after restoration. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows permissions: writer creation reports permission denied", "[windows][permissions]")
{
    auto const instance = createInstance(domain);
    auto const definition = mxl::tests::readFile("data/audio_flow.json");
    auto writer = mxlFlowWriter{};
    auto denied = ScopedDeniedAccess{domain, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY};
    REQUIRE(mxlCreateFlowWriter(instance.get(), definition.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_ERR_PERMISSION_DENIED);
    REQUIRE(writer == nullptr);
    REQUIRE(denied.restore() == ERROR_SUCCESS);
    REQUIRE(mxlCreateFlowWriter(instance.get(), definition.c_str(), nullptr, &writer, nullptr, nullptr) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(instance.get(), writer) == MXL_STATUS_OK);
}

/** @test The public reader API reports permission denied for flow files that the reader may not read. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows permissions: reader creation reports permission denied", "[windows][permissions]")
{
    // Windows grants every user the right to bypass traverse checking, so a denial on the flow directory does not stop a reader. The
    // denial is on the file the reader opens instead. Handles that the writer already holds keep their access.
    // The same cases as the POSIX tests: a flow whose files the reader may not open, and a continuous flow whose descriptor it may not read.
    auto const [definitionFile, deniedFile] = GENERATE(table<char const*, char const*>({
        {"data/v210_flow.json",  "data"         },
        {"data/audio_flow.json", "flow_def.json"},
    }));
    CAPTURE(definitionFile, deniedFile);
    auto const instance = createInstance(domain);
    auto const readerInstance = createInstance(domain);
    auto const definition = mxl::tests::readFile(definitionFile);
    auto writer = mxlFlowWriter{};
    auto config = mxlFlowConfigInfo{};
    REQUIRE(mxlCreateFlowWriter(instance.get(), definition.c_str(), nullptr, &writer, &config, nullptr) == MXL_STATUS_OK);
    auto const id = uuids::to_string(config.common.id);
    auto const path = makeFlowDirectoryName(domain, id) / deniedFile;

    auto denied = ScopedDeniedAccess{path, FILE_READ_DATA};
    auto reader = mxlFlowReader{};
    auto const status = mxlCreateFlowReader(readerInstance.get(), id.c_str(), "", &reader);
    REQUIRE(denied.restore() == ERROR_SUCCESS);
    REQUIRE(status == MXL_ERR_PERMISSION_DENIED);
    REQUIRE(mxlReleaseFlowWriter(instance.get(), writer) == MXL_STATUS_OK);
}

/** @test A denied deletion returns false and preserves the flow's data until permissions allow removal. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows permissions: FlowManager preserves a flow when deletion is denied",
    "[windows][permissions]")
{
    auto manager = FlowManager{domain};
    auto const definition = mxl::tests::readFile("data/audio_flow.json");
    auto const id = FlowParser{definition}.getId();
    auto const rate = mxlRational{.numerator = 48000, .denominator = 1};
    auto const directory = makeFlowDirectoryName(domain, uuids::to_string(id));
    auto [created, flow] = manager.createOrOpenContinuousFlow(id, definition, MXL_DATA_FORMAT_AUDIO, rate, 2, sizeof(float), 128);
    REQUIRE(created);
    flow->flowInfo()->runtime.headIndex = 12345;
    // The flow stays open: its data lives only while something maps it.
    {
        auto deniedParent = ScopedDeniedAccess{domain, FILE_DELETE_CHILD};
        auto deniedFlow = ScopedDeniedAccess{directory, FILE_LIST_DIRECTORY | DELETE | FILE_DELETE_CHILD};
        auto const probe = ::CreateFileW(directory.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        auto const probeError = ::GetLastError();
        if (probe != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(probe);
        }
        REQUIRE(probe == INVALID_HANDLE_VALUE);
        REQUIRE(probeError == ERROR_ACCESS_DENIED);
        REQUIRE_FALSE(manager.deleteFlow(id));
        REQUIRE(std::filesystem::exists(directory));
        REQUIRE(deniedFlow.restore() == ERROR_SUCCESS);
        REQUIRE(deniedParent.restore() == ERROR_SUCCESS);
    }
    auto reopened = manager.openFlow(id, AccessMode::READ_ONLY);
    REQUIRE(reopened->flowInfo()->runtime.headIndex == 12345);
    reopened.reset();
    flow.reset();
    REQUIRE(manager.deleteFlow(id));
    REQUIRE_FALSE(std::filesystem::exists(directory));
}

/** @test Event reader acquisition validates the last slot's header in the actual named memory section. */
TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Windows corruption: event readers validate every slot in the named section",
    "[windows][events][corruption]")
{
    auto const producer = createInstance(domain);
    auto const consumer = createInstance(domain);
    auto const definition = mxl::tests::readFile("data/event_flow.json");
    auto writer = mxlFlowWriter{};
    auto reader = mxlFlowReader{};
    auto config = mxlFlowConfigInfo{};
    REQUIRE(mxlCreateFlowWriter(producer.get(), definition.c_str(), nullptr, &writer, &config, nullptr) == MXL_STATUS_OK);
    auto const id = uuids::to_string(config.common.id);
    REQUIRE(mxlCreateFlowReader(consumer.get(), id.c_str(), nullptr, &reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(consumer.get(), reader) == MXL_STATUS_OK);
    reader = nullptr;

    auto const eventFile = makeEventDataFilePath(makeFlowDirectoryName(domain, id));
    auto mapping = SharedMemorySegment{eventFile.string().c_str(), AccessMode::READ_WRITE, 0, LockMode::None};
    auto const lastSlotOffset =
        sizeof(EventRingHeader) + ((config.event.eventCount - 1) * EventRingBuffer::slotStride(config.event.eventPayloadSize));
    auto* const lastSlot = reinterpret_cast<Event*>(static_cast<std::uint8_t*>(mapping.data()) + lastSlotOffset);
    auto const originalVersion = lastSlot->header.version;
    lastSlot->header.version = 0;
    REQUIRE(mxlCreateFlowReader(consumer.get(), id.c_str(), nullptr, &reader) != MXL_STATUS_OK);
    REQUIRE(reader == nullptr);

    lastSlot->header.version = originalVersion;
    REQUIRE(mxlCreateFlowReader(consumer.get(), id.c_str(), nullptr, &reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowReader(consumer.get(), reader) == MXL_STATUS_OK);
    REQUIRE(mxlReleaseFlowWriter(producer.get(), writer) == MXL_STATUS_OK);
}
