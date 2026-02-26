#! /bin/bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

example_dir="$(realpath "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"/..)"

if [[ -z "${1}" ]] || [[ "${1}" == "-h" ]]; then
    echo "Usage: ${0} <TARGET-NODE-HOSTNAME> [OUT-FILE]"
    exit 1
fi

if [[ -z "${2}" ]] || [[ "${2}" == "-" ]]; then
    out_file="/dev/stdout"
else
    out_file="${2}"
fi

>&2 echo "Rendering template at ${example_dir}/kube-example.yaml to ${out_file}"
sed "s/__HOSTNAME__/${1}/g" <"${example_dir}/kube-example.yaml" >"${out_file}"
