#! /bin/bash

target_dir="${1}"
if [[ -z "${target_dir}" ]]; then
    echo "Usage: ${0} <TARGET>"
    exit 1
fi

domain_dir="$(docker inspect mxl-example_mxl-domain | jq -r '.[0] | .Mountpoint')"
if [[ -z "${domain_dir}" || "${domain_dir}" == "null" ]]; then
    echo "Failed to get local volume directory."
    exit 1
fi

mkdir -vp "${target_dir}"
target_dir="$(realpath "${target_dir}")"

echo "Local volume directory: ${domain_dir}"
echo "Creating bind mount to: ${target_dir}, this might require elevated permissions"
sudo mount --bind "${domain_dir}" "${target_dir}" -o "uid=$(id -u),gid=$(id -g)"
