// SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
// SPDX-License-Identifier: Apache-2.0

#include <memory>
#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include "mxl-internal/DomainWatcher.hpp"
#include "mxl-internal/Instance.hpp"
#include "mxl-internal/PosixFlowIoFactory.hpp"
#include "../../tests/Utils.hpp"

using namespace mxl::lib;

TEST_CASE_PERSISTENT_FIXTURE(mxl::tests::mxlDomainFixture, "Instance : getFlowReader rejects invalid flow ids", "[instance]")
{
    auto domainWatcher = std::make_shared<DomainWatcher>(domain);
    auto flowIoFactory = std::make_unique<PosixFlowIoFactory>(domainWatcher);
    auto const instance = std::make_shared<Instance>(domain, "", std::move(flowIoFactory), domainWatcher);

    // The C API checks flow ids before calling getFlowReader, but internal callers do not.
    REQUIRE_THROWS_AS(instance->getFlowReader(""), std::invalid_argument);
    REQUIRE_THROWS_AS(instance->getFlowReader("not-a-uuid"), std::invalid_argument);
    REQUIRE_THROWS_AS(instance->getFlowReader("5fbec3b1-1b0f-417d-9059-8b94a47197e"), std::invalid_argument);
}
