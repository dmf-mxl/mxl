#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Same-node, same-GPU path: one MXL domain, writer cudaMalloc, reader CUDA IPC.
# This is the two-pod-same-GPU model without Kubernetes.
#
# Usage (from repo root, after building):
#   ./examples/scripts/cuda-ipc-same-gpu.sh
#   GPU=0 ./examples/scripts/cuda-ipc-same-gpu.sh

set -euo pipefail

repo_root="$(realpath "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"/../..)"
build_dir="${MXL_BUILD_DIR:-${repo_root}/build/Linux-Clang-Debug}"
testsrc="${build_dir}/tools/mxl-gst/mxl-gst-testsrc"
sink="${build_dir}/tools/mxl-gst/mxl-gst-sink"
flow_json="${repo_root}/examples/flow-configs/flow-video-v210.json"
src_opts="${repo_root}/examples/payload-options/cuda-payload-options.json"
flow_id="5fbec3b1-1b0f-417d-9059-8b94a47197ed"

domain="${MXL_DOMAIN:-/dev/shm/mxl-ipc}"
gpu="${GPU:-0}"
seconds="${SECONDS_TO_RUN:-15}"

if [[ ! -x "${testsrc}" ]]; then
    echo "missing ${testsrc}; set MXL_BUILD_DIR or build mxl-gst-testsrc" >&2
    exit 1
fi
if [[ ! -x "${sink}" ]]; then
    echo "missing ${sink}; set MXL_BUILD_DIR or build mxl-gst-sink" >&2
    exit 1
fi

cleanup() {
    [[ -n "${testsrc_pid:-}" ]] && kill "${testsrc_pid}" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "${domain}"
rm -rf "${domain}/${flow_id}.mxl-flow"

src_opts_effective="${src_opts}"
if [[ "${gpu}" != "0" ]]; then
    src_opts_effective="$(mktemp)"
    python3 - "${gpu}" "${src_opts_effective}" <<'PY'
import json, sys
idx, path = int(sys.argv[1]), sys.argv[2]
json.dump({"payload": {"location": "device", "deviceIndex": idx, "backend": "cuda-linear"}}, open(path, "w"), indent=2)
PY
fi

echo "Writer domain=${domain} GPU=${gpu}"
"${testsrc}" \
    -d "${domain}" \
    -v "${flow_json}" \
    --video-options-file "${src_opts_effective}" \
    -p ball \
    >/tmp/mxl-gst-testsrc-ipc.log 2>&1 &
testsrc_pid=$!
sleep 2
if ! kill -0 "${testsrc_pid}" 2>/dev/null; then
    echo "gst-testsrc failed:" >&2
    cat /tmp/mxl-gst-testsrc-ipc.log >&2
    exit 1
fi

echo "Reader same domain, same GPU (CUDA IPC, no --device-index)"
timeout "${seconds}" "${sink}" \
    -d "${domain}" \
    -v "${flow_id}" \
    | tee /tmp/mxl-gst-sink-ipc.log
