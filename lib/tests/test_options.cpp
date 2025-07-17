#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "../src/internal/Instance.hpp"
#include "../src/internal/PosixFlowIoFactory.hpp"
#include "Utils.hpp"

using namespace mxl::lib;

namespace
{
    auto const options_500 = R"({"urn:x-mxl:option:history_duration/v1.0": 500000000})";
    auto const options_250 = R"({"urn:x-mxl:option:history_duration/v1.0": 250000000})";
}

TEST_CASE("Options : Default value", "[options]")
{
    // Use different domains per test to allow for concurrent execution of tests
    auto const domain = mxl::tests::getDomainPath() / "options_test_1";

    // Remove that path if it exists.
    remove_all(domain);

    // Create the mxl domain path.
    REQUIRE(create_directories(domain));

    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>();
    auto const instance = std::make_shared<Instance>(domain, "", std::move(flowIoFactory));

    REQUIRE(instance->getHistoryDurationNs() == 100'000'000ULL); // default value

    // cleanup
    remove_all(domain);
}

TEST_CASE("Options : Domain config", "[options]")
{
    auto const domain = mxl::tests::getDomainPath() / "options_test_2";

    // Remove that path if it exists.
    remove_all(domain);

    // Create the mxl domain path.
    REQUIRE(create_directories(domain));

    // write the domain options file
    auto const domainOptionsFile = domain / ".options";
    std::ofstream ofs(domainOptionsFile);
    REQUIRE(ofs.is_open());
    ofs << options_500;
    ofs.close();

    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>();
    auto const instance = std::make_shared<Instance>(domain, "", std::move(flowIoFactory));

    REQUIRE(instance->getHistoryDurationNs() == 500'000'000ULL); // domain value

    // cleanup
    remove_all(domain);
}

TEST_CASE("Options : Instance config", "[options]")
{
    auto const domain = mxl::tests::getDomainPath() / "options_test_3";

    // Remove that path if it exists.
    remove_all(domain);

    // Create the mxl domain path.
    REQUIRE(create_directories(domain));

    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>();
    auto const instance = std::make_shared<Instance>(domain, options_250, std::move(flowIoFactory));

    // We should get the default value, not the instance config value.
    // We don't want per-instance history durations.
    REQUIRE(instance->getHistoryDurationNs() == 100'000'000ULL);

    // cleanup
    remove_all(domain);
}

TEST_CASE("Options : Domain + Instance config", "[options]")
{
    auto const domain = mxl::tests::getDomainPath() / "options_test_4";

    // Remove that path if it exists.
    remove_all(domain);

    // Create the mxl domain path.
    REQUIRE(create_directories(domain));

    // write the domain options file
    auto const domainOptionsFile = domain / ".options";
    std::ofstream ofs(domainOptionsFile);
    REQUIRE(ofs.is_open());
    ofs << options_500;
    ofs.close();

    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>();
    auto const instance = std::make_shared<Instance>(domain, options_250, std::move(flowIoFactory));

    // We should use the domain config, not the instance config.  We don't want per-instance history durations.
    REQUIRE(instance->getHistoryDurationNs() == 500'000'000ULL);

    // cleanup
    remove_all(domain);
}

TEST_CASE("Options : invalid configs", "[options]")
{
    auto const domain = mxl::tests::getDomainPath() / "options_test_5";

    // Remove that path if it exists.
    remove_all(domain);

    // Create the mxl domain path.
    REQUIRE(create_directories(domain));

    // write the domain options file
    auto const domainOptionsFile = domain / ".options";
    std::ofstream ofs(domainOptionsFile);
    REQUIRE(ofs.is_open());
    ofs << "abc";
    ofs.close();

    auto flowIoFactory = std::make_unique<mxl::lib::PosixFlowIoFactory>();
    auto const instance = std::make_shared<Instance>(domain, "def", std::move(flowIoFactory));

    REQUIRE(instance->getHistoryDurationNs() == 100'000'000ULL); // default value

    // Clean up the domain path
    remove_all(domain);
}
