#! /bin/bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

example_dir="$(realpath "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"/..)"

if [[ -z "${1}" ]] || [[ "${1}" == "-h" ]] || [[ -z "${2}" ]]; then
    echo "Usage: ${0} <SRC-NODE-HOSTNAME> <DST-NODE-HOSTNAME> [OUT-FILE]"
    exit 1
fi

if [[ -z "${3}" ]] || [[ "${3}" == "-" ]]; then
    out_file="/dev/stdout"
else
    out_file="${3}"
fi

>&2 echo "Rendering template at ${example_dir}/kube-fabrics-cuda-two-domain.yaml to ${out_file}"
sed -e "s/__SRC_HOSTNAME__/${1}/g" -e "s/__DST_HOSTNAME__/${2}/g" \
    <"${example_dir}/kube-fabrics-cuda-two-domain.yaml" >"${out_file}"
