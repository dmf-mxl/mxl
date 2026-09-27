#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Same-node two-GPU path: one MXL domain, writer cudaMalloc on SRC_GPU,
# reader maps those grains onto DST_GPU with CUDA IPC peer access.
# No fabrics / no second domain. Requires NVLink or P2P-capable PCIe.
#
# Usage (from repo root, after building):
#   SRC_GPU=0 DST_GPU=1 ./examples/scripts/cuda-p2p-two-gpu.sh

set -euo pipefail

repo_root="$(realpath "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"/../..)"
build_dir="${MXL_BUILD_DIR:-${repo_root}/build/Linux-Clang-Debug}"
testsrc="${build_dir}/tools/mxl-gst/mxl-gst-testsrc"
sink="${build_dir}/tools/mxl-gst/mxl-gst-sink"
flow_json="${repo_root}/examples/flow-configs/flow-video-v210.json"
src_opts="${repo_root}/examples/payload-options/cuda-payload-options.json"
flow_id="5fbec3b1-1b0f-417d-9059-8b94a47197ed"

domain="${MXL_DOMAIN:-/dev/shm/mxl-p2p}"
src_gpu="${SRC_GPU:-0}"
dst_gpu="${DST_GPU:-1}"
seconds="${SECONDS_TO_RUN:-15}"

if [[ ! -x "${testsrc}" ]]; then
    echo "missing ${testsrc}; set MXL_BUILD_DIR or build mxl-gst-testsrc" >&2
    exit 1
fi
if [[ ! -x "${sink}" ]]; then
    echo "missing ${sink}; set MXL_BUILD_DIR or build mxl-gst-sink" >&2
    exit 1
fi
if [[ "${src_gpu}" == "${dst_gpu}" ]]; then
    echo "SRC_GPU and DST_GPU must differ for the P2P path (got ${src_gpu})" >&2
    exit 1
fi

cleanup() {
    [[ -n "${testsrc_pid:-}" ]] && kill "${testsrc_pid}" 2>/dev/null || true
    [[ -n "${sink_pid:-}" ]] && kill "${sink_pid}" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "${domain}"
rm -rf "${domain}/${flow_id}.mxl-flow"

src_opts_effective="${src_opts}"
if [[ "${src_gpu}" != "0" ]]; then
    src_opts_effective="$(mktemp)"
    python3 - "${src_gpu}" "${src_opts_effective}" <<'PY'
import json, sys
idx, path = int(sys.argv[1]), sys.argv[2]
json.dump({"payload": {"location": "device", "deviceIndex": idx, "backend": "cuda-linear"}}, open(path, "w"), indent=2)
PY
fi

echo "Writer domain=${domain} GPU=${src_gpu}"
"${testsrc}" \
    -d "${domain}" \
    -v "${flow_json}" \
    --video-options-file "${src_opts_effective}" \
    -p ball \
    >/tmp/mxl-gst-testsrc-p2p.log 2>&1 &
testsrc_pid=$!
sleep 2
if ! kill -0 "${testsrc_pid}" 2>/dev/null; then
    echo "gst-testsrc failed:" >&2
    cat /tmp/mxl-gst-testsrc-p2p.log >&2
    exit 1
fi

echo "Reader same domain GPU=${dst_gpu} (CUDA IPC P2P)"
timeout "${seconds}" "${sink}" \
    -d "${domain}" \
    -v "${flow_id}" \
    --device-index "${dst_gpu}" \
    | tee /tmp/mxl-gst-sink-p2p.log
