#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Two independent MXL domains, each with its own CUDA payload flow.
# Simulates a writer and initiator on one GPU (or node) and a fabrics target on
# another. CUDA IPC is not shared between the domains; fabrics RDMA copies grains.
#
# Usage (from repo root, after building with MXL_ENABLE_CUDA_PAYLOAD and MXL_ENABLE_FABRICS_OFI):
#   SRC_GPU=0 DST_GPU=0 ./examples/scripts/fabrics-cuda-two-domain.sh
# Cross-GPU on one node:
#   SRC_GPU=0 DST_GPU=1 ./examples/scripts/fabrics-cuda-two-domain.sh
#
# The GPUs are selected by index. The script reads their UUIDs with nvidia-smi
# and passes them to the payload backend as deviceUuid.
#
# Requires tmpfs domains (default /dev/shm/...). Ext4 /tmp cannot pin verbs MRs on many hosts.

set -euo pipefail

repo_root="$(realpath "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"/../..)"
build_dir="${MXL_BUILD_DIR:-${repo_root}/build/Linux-Clang-Debug}"
demo="${build_dir}/tools/mxl-fabrics-demo/mxl-fabrics-demo"
testsrc="${build_dir}/tools/mxl-gst/mxl-gst-cuda-testsrc"
flow_json="${repo_root}/examples/flow-configs/flow-video-v210.json"
flow_id="5fbec3b1-1b0f-417d-9059-8b94a47197ed"

src_domain="${SRC_DOMAIN:-/dev/shm/mxl-src}"
dst_domain="${DST_DOMAIN:-/dev/shm/mxl-dst}"
src_gpu="${SRC_GPU:-0}"
dst_gpu="${DST_GPU:-0}"
target_port="${TARGET_PORT:-1234}"
initiator_port="${INITIATOR_PORT:-1235}"
target_info="${TARGET_INFO:-/tmp/mxl-target-info.bin}"
seconds="${SECONDS_TO_RUN:-15}"

if [[ ! -x "${demo}" ]]; then
    echo "missing ${demo}; set MXL_BUILD_DIR or build mxl-fabrics-demo" >&2
    exit 1
fi
if [[ ! -x "${testsrc}" ]]; then
    echo "missing ${testsrc}; set MXL_BUILD_DIR or build mxl-gst-cuda-testsrc" >&2
    exit 1
fi

# Print the UUID of the GPU with the given index, for example GPU-1234....
gpu_uuid() {
    local uuid
    uuid="$(nvidia-smi -i "$1" --query-gpu=uuid --format=csv,noheader)"
    if [[ -z "${uuid}" ]]; then
        echo "no GPU with index $1" >&2
        exit 1
    fi
    printf '%s\n' "${uuid}"
}

src_uuid="$(gpu_uuid "${src_gpu}")"
dst_uuid="$(gpu_uuid "${dst_gpu}")"

# The target creates its flow with the payload on the destination GPU.
dst_opts="$(mktemp)"
printf '{"payload":{"backend":"cuda-linear","deviceUuid":"%s"}}\n' "${dst_uuid}" >"${dst_opts}"

cleanup() {
    [[ -n "${testsrc_pid:-}" ]] && kill "${testsrc_pid}" 2>/dev/null || true
    [[ -n "${target_pid:-}" ]] && kill "${target_pid}" 2>/dev/null || true
    [[ -n "${initiator_pid:-}" ]] && kill "${initiator_pid}" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -f "${dst_opts}"
}
trap cleanup EXIT

mkdir -p "${src_domain}" "${dst_domain}"
rm -rf "${src_domain}/${flow_id}.mxl-flow" "${dst_domain}/${flow_id}.mxl-flow"
rm -f "${target_info}"

echo "Starting target domain=${dst_domain} GPU=${dst_gpu} (${dst_uuid})"
"${demo}" \
    -d "${dst_domain}" \
    -f "${flow_json}" \
    --flow-options "${dst_opts}" \
    -p verbs \
    -s "${target_port}" \
    --target-info "${target_info}" \
    >/tmp/mxl-fabrics-target.log 2>&1 &
target_pid=$!

for _ in $(seq 1 40); do
    if grep -q 'Target info:' /tmp/mxl-fabrics-target.log 2>/dev/null; then
        break
    fi
    if ! kill -0 "${target_pid}" 2>/dev/null; then
        echo "target failed:" >&2
        cat /tmp/mxl-fabrics-target.log >&2
        exit 1
    fi
    sleep 0.25
done
if [[ ! -s "${target_info}" ]]; then
    echo "target-info not written; see /tmp/mxl-fabrics-target.log" >&2
    exit 1
fi

echo "Starting writer domain=${src_domain} GPU=${src_gpu} (${src_uuid})"
"${testsrc}" \
    -d "${src_domain}" \
    -v "${flow_json}" \
    --device-uuid "${src_uuid}" \
    -p ball \
    >/tmp/mxl-gst-testsrc.log 2>&1 &
testsrc_pid=$!
sleep 2
if ! kill -0 "${testsrc_pid}" 2>/dev/null; then
    echo "mxl-gst-cuda-testsrc failed:" >&2
    cat /tmp/mxl-gst-testsrc.log >&2
    exit 1
fi

echo "Starting initiator (reads ${src_domain}, RDMA to target)"
# The initiator runs until timeout stops it, which exits with status 124. That is a normal end here.
initiator_status=0
timeout "${seconds}" "${demo}" \
    -i \
    -d "${src_domain}" \
    -f "${flow_id}" \
    -p verbs \
    -s "${initiator_port}" \
    --target-info "@${target_info}" \
    | tee /tmp/mxl-fabrics-init.log || initiator_status=$?
if [[ "${initiator_status}" -ne 0 && "${initiator_status}" -ne 124 ]]; then
    echo "initiator failed with status ${initiator_status}; see /tmp/mxl-fabrics-init.log" >&2
    exit "${initiator_status}"
fi

echo
echo "Target log tail:"
grep -E 'Failed|error|grains/s' /tmp/mxl-fabrics-target.log | tail -20 || true
