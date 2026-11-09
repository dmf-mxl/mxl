#!/bin/bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

# Run the CUDA payload writer and reader in two containers.
# Both containers bind-mount one host tmpfs directory and use it as the MXL domain.
# The payload file descriptor travels on a socket in that directory, so the containers do not
# share an IPC namespace or a PID namespace.
#
#   examples/cuda-cross-container-test.sh
#
# MXL_CUDA_TEST_IMAGE selects the image (default mxl-cuda-tests).
# MXL_DOCKER_EXTRA is appended to both docker run commands. This host needs
# MXL_DOCKER_EXTRA=--privileged because --gpus all alone does not initialize NVML.

set -euo pipefail

image="${MXL_CUDA_TEST_IMAGE:-mxl-cuda-tests}"
name="mxl-cuda-writer-$$"
domain="/dev/shm/mxl-cuda-cross"
flow_id="5fbec3b1-1b0f-417d-9059-8b94a47197ed"
byte=92

if docker info >/dev/null 2>&1; then
    docker=(docker)
elif sudo docker info >/dev/null 2>&1; then
    docker=(sudo docker)
else
    echo "mxl-cuda-cross-container: docker is not available" >&2
    exit 2
fi

extra=()
if [[ -n "${MXL_DOCKER_EXTRA:-}" ]]; then
    # shellcheck disable=SC2206
    extra=(${MXL_DOCKER_EXTRA})
fi

host_domain="$(mktemp -d /dev/shm/mxl-cuda-cross.XXXXXX)"

cleanup()
{
    "${docker[@]}" rm -f "${name}" >/dev/null 2>&1 || true
    if [[ -n "${host_domain:-}" ]]; then
        rm -rf "${host_domain}" 2>/dev/null || true
        # The containers run as root, so the flow files may not be removable by this user.
        if [[ -d "${host_domain}" ]]; then
            "${docker[@]}" run --rm -v /dev/shm:/dev/shm --entrypoint rm "${image}" -rf "${host_domain}" >/dev/null 2>&1 || true
        fi
    fi
}
trap cleanup EXIT

if ! "${docker[@]}" image inspect "${image}" >/dev/null 2>&1; then
    echo "mxl-cuda-cross-container: image ${image} is not available" >&2
    exit 1
fi

"${docker[@]}" rm -f "${name}" >/dev/null 2>&1 || true
"${docker[@]}" run -d --name "${name}" \
    --gpus all \
    -v "${host_domain}:${domain}" \
    "${extra[@]}" \
    --entrypoint /opt/mxl/bin/mxl-cuda-writer-helper \
    "${image}" \
    "${domain}" /opt/mxl/data/v210_flow.json "${byte}"

index=""
for _ in $(seq 1 50); do
    if ! "${docker[@]}" inspect -f '{{.State.Running}}' "${name}" 2>/dev/null | grep -q true; then
        "${docker[@]}" logs "${name}" >&2 || true
        echo "mxl-cuda-cross-container: writer exited before READY" >&2
        exit 1
    fi
    logs="$("${docker[@]}" logs "${name}" 2>&1 || true)"
    while IFS= read -r line; do
        case "${line}" in
            READY\ *)
                index="${line#READY }"
                break
                ;;
        esac
    done <<< "${logs}"
    if [[ -n "${index}" ]]; then
        break
    fi
    sleep 0.2
done
if [[ -z "${index}" ]]; then
    "${docker[@]}" logs "${name}" >&2 || true
    echo "mxl-cuda-cross-container: writer did not become ready" >&2
    exit 1
fi

"${docker[@]}" run --rm \
    --gpus all \
    -v "${host_domain}:${domain}" \
    "${extra[@]}" \
    --entrypoint /opt/mxl/bin/mxl-cuda-reader-helper \
    "${image}" \
    "${domain}" "${flow_id}" "${index}" "${byte}"
