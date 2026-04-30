#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# EFA deployment verification tests.
# Run after deploying kube-aws-2-nodes-efa.yaml against an EFA-capable EKS cluster.
# Usage: ./test-efa-deployment.sh
# Exit code 0 = all tests passed, 1 = one or more tests failed.

set -euo pipefail

READER_DEPLOY="reader-media-function-efa"
WRITER_DEPLOY="writer-media-function-efa"
READER_CONTAINER="reader-container"
WRITER_CONTAINER="writer-container"

PASS=0
FAIL=0
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
info() { echo -e "      $1"; }

echo ""
echo "======================================================"
echo " EFA 2-Node Deployment — Verification Tests"
echo "======================================================"
echo ""

# ---------------------------------------------------------------------------
# T1: Both pods Running 2/2 with 0 restarts
# ---------------------------------------------------------------------------
echo "T1: Both pods Running 2/2 with zero restarts"

READER_STATUS=$(kubectl get pod -l app=${READER_DEPLOY} -o jsonpath='{.items[0].status.phase}' 2>/dev/null || echo "NotFound")
WRITER_STATUS=$(kubectl get pod -l app=${WRITER_DEPLOY} -o jsonpath='{.items[0].status.phase}' 2>/dev/null || echo "NotFound")
READER_READY=$(kubectl get pod -l app=${READER_DEPLOY} -o jsonpath='{.items[0].status.containerStatuses[0].ready},{.items[0].status.containerStatuses[1].ready}' 2>/dev/null || echo "false,false")
WRITER_READY=$(kubectl get pod -l app=${WRITER_DEPLOY} -o jsonpath='{.items[0].status.containerStatuses[0].ready}' 2>/dev/null || echo "false")
READER_RESTARTS=$(kubectl get pod -l app=${READER_DEPLOY} -o jsonpath='{.items[0].status.containerStatuses[0].restartCount}' 2>/dev/null || echo "-1")
WRITER_RESTARTS=$(kubectl get pod -l app=${WRITER_DEPLOY} -o jsonpath='{.items[0].status.containerStatuses[0].restartCount}' 2>/dev/null || echo "-1")

if [ "$READER_STATUS" = "Running" ] && [ "$WRITER_STATUS" = "Running" ]; then
    pass "Both pods in Running phase"
else
    fail "Pod phase — reader: ${READER_STATUS}, writer: ${WRITER_STATUS}"
fi

if [ "$READER_READY" = "true,true" ]; then
    pass "Reader: 2/2 containers ready"
else
    fail "Reader containers not ready: ${READER_READY}"
fi

if [ "$WRITER_READY" = "true" ]; then
    pass "Writer: 1/1 container ready"
else
    fail "Writer container not ready"
fi

if [ "$READER_RESTARTS" -eq 0 ] && [ "$WRITER_RESTARTS" -eq 0 ]; then
    pass "Zero restarts on both pods"
else
    fail "Restarts detected — reader: ${READER_RESTARTS}, writer: ${WRITER_RESTARTS}"
fi

# ---------------------------------------------------------------------------
# T2: Pods are on different nodes (EFA anti-affinity)
# ---------------------------------------------------------------------------
echo ""
echo "T2: Pods scheduled on different nodes (podAntiAffinity)"

READER_NODE=$(kubectl get pod -l app=${READER_DEPLOY} -o jsonpath='{.items[0].spec.nodeName}' 2>/dev/null)
WRITER_NODE=$(kubectl get pod -l app=${WRITER_DEPLOY} -o jsonpath='{.items[0].spec.nodeName}' 2>/dev/null)

info "Reader node: ${READER_NODE}"
info "Writer node: ${WRITER_NODE}"

if [ -n "$READER_NODE" ] && [ -n "$WRITER_NODE" ] && [ "$READER_NODE" != "$WRITER_NODE" ]; then
    pass "Reader and writer on different nodes"
else
    fail "Reader and writer on the SAME node (EFA anti-affinity violation)"
fi

# ---------------------------------------------------------------------------
# T3: EFA device present on both nodes
# ---------------------------------------------------------------------------
echo ""
echo "T3: EFA device (/dev/infiniband/uverbs0) present in both containers"

READER_POD=$(kubectl get pod -l app=${READER_DEPLOY} -o jsonpath='{.items[0].metadata.name}')
WRITER_POD=$(kubectl get pod -l app=${WRITER_DEPLOY} -o jsonpath='{.items[0].metadata.name}')

if kubectl exec "${READER_POD}" -c "${READER_CONTAINER}" -- sh -c "test -c /dev/infiniband/uverbs0" 2>/dev/null; then
    pass "Reader: EFA device /dev/infiniband/uverbs0 present"
else
    fail "Reader: EFA device NOT found"
fi

if kubectl exec "${WRITER_POD}" -c "${WRITER_CONTAINER}" -- sh -c "test -c /dev/infiniband/uverbs0" 2>/dev/null; then
    pass "Writer: EFA device /dev/infiniband/uverbs0 present"
else
    fail "Writer: EFA device NOT found"
fi

# ---------------------------------------------------------------------------
# T4: EFA device ACTIVE state
# ---------------------------------------------------------------------------
echo ""
echo "T4: EFA device in ACTIVE state"

READER_EFA_STATE=$(kubectl exec "${READER_POD}" -c "${READER_CONTAINER}" -- sh -c "cat /sys/class/infiniband/*/ports/1/state 2>/dev/null | head -1" || echo "unknown")
WRITER_EFA_STATE=$(kubectl exec "${WRITER_POD}" -c "${WRITER_CONTAINER}" -- sh -c "cat /sys/class/infiniband/*/ports/1/state 2>/dev/null | head -1" || echo "unknown")

if echo "$READER_EFA_STATE" | grep -q "ACTIVE"; then
    pass "Reader: EFA device state ACTIVE"
else
    fail "Reader: EFA device not ACTIVE (state: ${READER_EFA_STATE})"
fi

if echo "$WRITER_EFA_STATE" | grep -q "ACTIVE"; then
    pass "Writer: EFA device state ACTIVE"
else
    fail "Writer: EFA device not ACTIVE (state: ${WRITER_EFA_STATE})"
fi

# ---------------------------------------------------------------------------
# T5: No libfabric provider parse errors
# ---------------------------------------------------------------------------
echo ""
echo "T5: No 'Failed to parse provider' errors in logs"

READER_PARSE_ERR=$(kubectl logs "${READER_POD}" -c "${READER_CONTAINER}" 2>/dev/null | grep "Failed to parse provider" || true)
WRITER_PARSE_ERR=$(kubectl logs "${WRITER_POD}" -c "${WRITER_CONTAINER}" 2>/dev/null | grep "Failed to parse provider" || true)

if [ -z "$READER_PARSE_ERR" ]; then
    pass "Reader: no libfabric provider errors"
else
    fail "Reader: found provider parse error — '${READER_PARSE_ERR}'"
fi

if [ -z "$WRITER_PARSE_ERR" ]; then
    pass "Writer: no libfabric provider errors"
else
    fail "Writer: found provider parse error — '${WRITER_PARSE_ERR}'"
fi

# ---------------------------------------------------------------------------
# T6: Reader published target-info (EFA fabric endpoint ready)
# ---------------------------------------------------------------------------
echo ""
echo "T6: Reader published EFA target-info"

READER_TARGET_INFO=$(kubectl logs "${READER_POD}" -c "${READER_CONTAINER}" 2>/dev/null | grep -E "Target info published|target-info" | head -1 || true)

if [ -n "$READER_TARGET_INFO" ]; then
    pass "Reader: target-info published"
    info "${READER_TARGET_INFO}"
else
    fail "Reader: target-info NOT published (EFA target failed to start)"
fi

TARGET_INFO_CONTENT=$(kubectl exec "${READER_POD}" -c target-info-server -- cat /target-info/target-info.txt 2>/dev/null || true)

if [ -n "$TARGET_INFO_CONTENT" ]; then
    pass "Reader: target-info.txt non-empty (sidecar serving)"
else
    fail "Reader: target-info.txt is empty or missing"
fi

# ---------------------------------------------------------------------------
# T7: fi_addr_efa:// — proves real EFA RDMA (not TCP fallback)
# ---------------------------------------------------------------------------
echo ""
echo "T7: Writer connected via fi_addr_efa:// (real RDMA, not TCP fallback)"

WRITER_EFA_ADDR=$(kubectl logs "${WRITER_POD}" -c "${WRITER_CONTAINER}" 2>/dev/null | grep "fi_addr_efa://" || true)

if [ -n "$WRITER_EFA_ADDR" ]; then
    pass "Writer: fi_addr_efa:// address confirmed — real EFA RDMA"
    info "${WRITER_EFA_ADDR}"
else
    fail "Writer: no fi_addr_efa:// found — likely TCP fallback or connection failed"
fi

# verify no TCP fallback used instead
WRITER_TCP_ADDR=$(kubectl logs "${WRITER_POD}" -c "${WRITER_CONTAINER}" 2>/dev/null | grep "fi_addr_tcp://" || true)
if [ -n "$WRITER_TCP_ADDR" ]; then
    fail "Writer: TCP fallback address detected — NOT using EFA RDMA"
    info "${WRITER_TCP_ADDR}"
else
    pass "Writer: no TCP fallback address (EFA-only)"
fi

# ---------------------------------------------------------------------------
# T8: Address Vector entry — RDMA connection established
# ---------------------------------------------------------------------------
echo ""
echo "T8: RDMA connection established (Address Vector entry)"

WRITER_AV=$(kubectl logs "${WRITER_POD}" -c "${WRITER_CONTAINER}" 2>/dev/null | grep "added to the Address Vector" || true)

if [ -n "$WRITER_AV" ]; then
    pass "Writer: remote endpoint added to Address Vector"
    info "${WRITER_AV}"
else
    fail "Writer: no Address Vector entry — RDMA connection not established"
fi

# ---------------------------------------------------------------------------
# T9: Batch size 1080 — full 1080p grains flowing over RDMA
# ---------------------------------------------------------------------------
echo ""
echo "T9: Grains flowing — batch size 1080 slices (full HD frame)"

WRITER_BATCH=$(kubectl logs "${WRITER_POD}" -c "${WRITER_CONTAINER}" 2>/dev/null | grep "batch size of 1080 slices" || true)

if [ -n "$WRITER_BATCH" ]; then
    pass "Writer: batch size 1080 slices — full 1080p RDMA grain transport confirmed"
    info "${WRITER_BATCH}"
else
    fail "Writer: expected batch size 1080 not found — grains may not be flowing"
fi

# ---------------------------------------------------------------------------
# T10: SRT NLB reachable
# ---------------------------------------------------------------------------
echo ""
echo "T10: SRT output — NLB endpoint assigned"

NLB=$(kubectl get svc reader-media-service-efa -o jsonpath='{.status.loadBalancer.ingress[0].hostname}' 2>/dev/null || true)

if [ -n "$NLB" ]; then
    pass "NLB hostname assigned: ${NLB}"
    info "VLC: srt://${NLB}:5000?mode=caller"
else
    fail "NLB hostname not yet assigned"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "======================================================"
TOTAL=$((PASS+FAIL))
if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}ALL ${TOTAL} TESTS PASSED — EFA RDMA confirmed ✓${NC}"
else
    echo -e "${RED}${FAIL}/${TOTAL} TESTS FAILED${NC}"
fi
echo "======================================================"
echo ""

[ "$FAIL" -eq 0 ]
