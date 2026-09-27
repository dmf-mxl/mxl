#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Two independent MXL domains, each with its own CUDA grain buffers.
# Simulates writer/initiator on one GPU (or node) and fabrics target on another:
# CUDA IPC is not shared; fabrics RDMA copies grains.
#
# Usage (from repo root, after building):
#   SRC_GPU=0 DST_GPU=0 ./examples/scripts/fabrics-cuda-two-domain.sh
# Cross-GPU on one node:
#   SRC_GPU=0 DST_GPU=1 ./examples/scripts/fabrics-cuda-two-domain.sh
#
# Requires tmpfs domains (default /dev/shm/...). Ext4 /tmp cannot pin verbs MRs on many hosts.

set -euo pipefail

repo_root="$(realpath "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"/../..)"
build_dir="${MXL_BUILD_DIR:-${repo_root}/build/Linux-Clang-Debug}"
demo="${build_dir}/tools/mxl-fabrics-demo/mxl-fabrics-demo"
testsrc="${build_dir}/tools/mxl-gst/mxl-gst-testsrc"
flow_json="${repo_root}/examples/flow-configs/flow-video-v210.json"
src_opts="${repo_root}/examples/payload-options/cuda-payload-options.json"
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
    echo "missing ${testsrc}; set MXL_BUILD_DIR or build mxl-gst-testsrc" >&2
    exit 1
fi

cleanup() {
    [[ -n "${testsrc_pid:-}" ]] && kill "${testsrc_pid}" 2>/dev/null || true
    [[ -n "${target_pid:-}" ]] && kill "${target_pid}" 2>/dev/null || true
    [[ -n "${initiator_pid:-}" ]] && kill "${initiator_pid}" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "${src_domain}" "${dst_domain}"
rm -rf "${src_domain}/${flow_id}.mxl-flow" "${dst_domain}/${flow_id}.mxl-flow"
rm -f "${target_info}"

echo "Starting target domain=${dst_domain} GPU=${dst_gpu}"
"${demo}" \
    -d "${dst_domain}" \
    -f "${flow_json}" \
    --flow-options "${src_opts}" \
    --device-index "${dst_gpu}" \
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

echo "Starting writer domain=${src_domain} GPU=${src_gpu}"
# gst-testsrc has no --device-index; point it at a matching options file or generate one.
src_opts_effective="${src_opts}"
if [[ "${src_gpu}" != "0" ]]; then
    src_opts_effective="$(mktemp)"
    python3 - "${src_gpu}" "${src_opts_effective}" <<'PY'
import json, sys
idx, path = int(sys.argv[1]), sys.argv[2]
json.dump({"payload": {"location": "device", "deviceIndex": idx, "backend": "cuda-linear"}}, open(path, "w"), indent=2)
PY
fi

"${testsrc}" \
    -d "${src_domain}" \
    -v "${flow_json}" \
    --video-options-file "${src_opts_effective}" \
    -p ball \
    >/tmp/mxl-gst-testsrc.log 2>&1 &
testsrc_pid=$!
sleep 2
if ! kill -0 "${testsrc_pid}" 2>/dev/null; then
    echo "gst-testsrc failed:" >&2
    cat /tmp/mxl-gst-testsrc.log >&2
    exit 1
fi

echo "Starting initiator (reads ${src_domain}, RDMA to target)"
timeout "${seconds}" "${demo}" \
    -i \
    -d "${src_domain}" \
    -f "${flow_id}" \
    -p verbs \
    -s "${initiator_port}" \
    --target-info "@${target_info}" \
    | tee /tmp/mxl-fabrics-init.log

echo
echo "Target log tail:"
grep -E 'deviceIndex|grains/s|Failed|error' /tmp/mxl-fabrics-target.log | tail -20 || true
