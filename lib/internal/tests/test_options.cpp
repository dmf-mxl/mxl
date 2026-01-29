// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/Instance.hpp"
#include "mxl-internal/PathUtils.hpp"
#include "mxl-internal/PosixFlowIoFactory.hpp"
#include "../../tests/Utils.hpp"

using namespace mxl::lib;

namespace
{
    auto const options_500 = R"({"urn:x-mxl:option:history_duration/v1.0": 500000000})";
    auto const options_250 = R"({"urn:x-mxl:option:history_duration/v1.0": 250000000})";
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Options : Default value", "[options]")
{
    auto domainWatcher = std::make_shared<DomainWatcher>(domain);
    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>(domainWatcher);
    auto const instance = std::make_shared<Instance>(domain, "", std::move(flowIoFactory), domainWatcher);

    REQUIRE(instance->getHistoryDurationNs() == 200'000'000ULL); // default value
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Options : Domain config", "[options]")
{
    // write the domain options file
    auto const domainOptionsFile = makeDomainOptionsFilePath(domain);
    std::ofstream ofs(domainOptionsFile);
    REQUIRE(ofs.is_open());
    ofs << options_500;
    ofs.close();

    auto domainWatcher = std::make_shared<DomainWatcher>(domain);
    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>(domainWatcher);
    auto const instance = std::make_shared<Instance>(domain, "", std::move(flowIoFactory), domainWatcher);

    REQUIRE(instance->getHistoryDurationNs() == 500'000'000ULL); // domain value
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Options : Instance config", "[options]")
{
    auto domainWatcher = std::make_shared<DomainWatcher>(domain);
    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>(domainWatcher);
    auto const instance = std::make_shared<Instance>(domain, options_250, std::move(flowIoFactory), domainWatcher);

    // We should get the default value, not the instance config value.
    // We don't want per-instance history durations.
    REQUIRE(instance->getHistoryDurationNs() == 200'000'000ULL);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Options : Domain + Instance config", "[options]")
{
    // write the domain options file
    auto const domainOptionsFile = makeDomainOptionsFilePath(domain);
    std::ofstream ofs(domainOptionsFile);
    REQUIRE(ofs.is_open());
    ofs << options_500;
    ofs.close();

    auto domainWatcher = std::make_shared<DomainWatcher>(domain);
    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>(domainWatcher);
    auto const instance = std::make_shared<Instance>(domain, options_250, std::move(flowIoFactory), domainWatcher);

    // We should use the domain config, not the instance config.  We don't want per-instance history durations.
    REQUIRE(instance->getHistoryDurationNs() == 500'000'000ULL);
}

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Options : invalid configs", "[options]")
{
    // write the domain options file
    auto const domainOptionsFile = makeDomainOptionsFilePath(domain);
    std::ofstream ofs(domainOptionsFile);
    REQUIRE(ofs.is_open());
    ofs << "abc";
    ofs.close();

    auto domainWatcher = std::make_shared<DomainWatcher>(domain);
    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>(domainWatcher);
    auto const instance = std::make_shared<Instance>(domain, "def", std::move(flowIoFactory), domainWatcher);

    REQUIRE(instance->getHistoryDurationNs() == 200'000'000ULL); // default value
}
