// SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <rdma/fabric.h>
#include "../NotifyEnvGuard.hpp"
#include "Address.hpp"
#include "FabricAddress.hpp"
#include "Notify.hpp"
#include "NotifyMode.hpp"
#include "Provider.hpp"
#include "TargetInfo.hpp"
#include "Util.hpp"

using namespace mxl::lib::fabrics::ofi;
using mxl::tests::NotifyEnvGuard;

namespace
{
    /** \brief Build a TargetInfo that advertises the given notify mode.
     * \param[in] notifyMode The notify mode the target advertises.
     * \return The TargetInfo.
     */
    [[nodiscard]]
    TargetInfo makeRemote(NotifyMode notifyMode)
    {
        return {.id = 1,
            .fabricAddress = RawFabricAddress::fromBase64("AgAjgn8AAAEAAAAAAAAAAA==", FabricAddressFormat::SockaddrIn),
            .provider = Provider::TCP,
            .remoteRegions = {},
            .bounceBufferInfo = std::nullopt,
            .notifyMode = notifyMode};
    }

    /** \brief Make a tcp domain behave like a provider that can or cannot deliver remote CQ data.
     * \param[in] domain The domain to change.
     * \param[in] cqData Whether the provider delivers remote CQ data.
     */
    void setCqDataSupport(Domain& domain, bool cqData)
    {
        domain.fabric()->info()->domain_attr->cq_data_size = cqData ? sizeof(std::uint64_t) : 0;
    }
}

TEST_CASE("ofi: Notify - Egress mode selection", "[ofi][Notify][NotifyMode]")
{
    auto const env = NotifyEnvGuard{nullptr};
    auto domain = getDomain();
    domain->fabric()->info()->tx_attr->msg_order |= FI_ORDER_SAW;
    domain->fabric()->info()->tx_attr->inject_size = 64;

    SECTION("Both sides use immediate data")
    {
        setCqDataSupport(*domain, true);
        REQUIRE(selectEgressNotifyMode(*domain, makeRemote(NotifyMode::Immediate)) == NotifyMode::Immediate);
    }

    SECTION("Target asks for send notifications")
    {
        setCqDataSupport(*domain, true);
        REQUIRE(selectEgressNotifyMode(*domain, makeRemote(NotifyMode::Send)) == NotifyMode::Send);
    }

    SECTION("Initiator cannot send immediate data")
    {
        setCqDataSupport(*domain, false);
        REQUIRE(selectEgressNotifyMode(*domain, makeRemote(NotifyMode::Immediate)) == NotifyMode::Send);
    }

    SECTION("Send notifications need send after write ordering")
    {
        domain->fabric()->info()->tx_attr->msg_order &= ~FI_ORDER_SAW;
        REQUIRE_THROWS(selectEgressNotifyMode(*domain, makeRemote(NotifyMode::Send)));
        // Immediate data does not depend on the ordering.
        setCqDataSupport(*domain, true);
        REQUIRE(selectEgressNotifyMode(*domain, makeRemote(NotifyMode::Immediate)) == NotifyMode::Immediate);
    }

    SECTION("Send notifications need room for the message in an inject")
    {
        domain->fabric()->info()->tx_attr->inject_size = notifyMessageSize - 1;
        REQUIRE_THROWS(selectEgressNotifyMode(*domain, makeRemote(NotifyMode::Send)));
    }
}
