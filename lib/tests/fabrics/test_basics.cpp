#include <cstring>
#include <catch2/catch_test_macros.hpp>
#include <mxl/fabrics.h>
#include <rdma/fabric.h>
#include <TargetInfo.hpp>
#include "mxl/mxl.h"

TEST_CASE("Fabrics : Test", "[fabrics]")
{
    auto instance = mxlCreateInstance("/dev/shm/_fabrics_basics", "");

    mxlFabricsInstance fabrics;
    mxlFabricsTarget target;
    mxlTargetInfo targetInfo;

    REQUIRE(mxlFabricsCreateInstance(instance, &fabrics) == MXL_STATUS_OK);

    REQUIRE(mxlFabricsCreateTarget(fabrics, &target) == MXL_STATUS_OK);

    // clang-format off
    auto config = mxlTargetConfig{
        .endpointAddress = mxlEndpointAddress{
            .node = "10.130.65.4",
            .service = "1234"
        },
        .provider = MXL_SHARING_PROVIDER_AUTO,
    .regions = nullptr,
    };
    // clang-format on

    REQUIRE(mxlFabricsTargetSetup(target, &config, &targetInfo) == MXL_STATUS_OK);

    REQUIRE(mxlFabricsDestroyTarget(fabrics, target) == MXL_STATUS_OK);

    REQUIRE(mxlFabricsDestroyInstance(fabrics) == MXL_STATUS_OK);

    REQUIRE(mxlDestroyInstance(instance) == MXL_STATUS_OK);
}

TEST_CASE("Fabrics: TargetInfo serialize", "[target-info]")
{
    namespace ofi = mxl::lib::fabrics::ofi;

    ofi::TargetInfo info;
    std::size_t ssize;

    REQUIRE(mxlFabricsTargetInfoToString(reinterpret_cast<mxlTargetInfo>(&info), nullptr, &ssize) == MXL_STATUS_OK);

    auto strbuf = reinterpret_cast<char*>(::malloc(ssize));

    REQUIRE(mxlFabricsTargetInfoToString(reinterpret_cast<mxlTargetInfo>(&info), strbuf, &ssize) == MXL_STATUS_OK);
    REQUIRE(::strcmp(strbuf, "{}") == 0);
}

TEST_CASE("Fabrics: TargetInfo deserialize", "[target-info]")
{
    mxlTargetInfo info;

    REQUIRE(mxlFabricsTargetInfoFromString("{}", &info) == MXL_STATUS_OK);

    REQUIRE(mxlFabricsFreeTargetInfo(info) == MXL_STATUS_OK);
}
