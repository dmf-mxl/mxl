#! /bin/bash

trap "exit 0" SIGINT SIGTERM

program_name="${0}"
while [[ $# -gt 0 ]]; do
    case $1 in
    -d | --domain)
        mxl_domain="${2}"
        shift
        shift
        ;;
    -f | --flow)
        mxl_flow_id="${2}"
        shift
        shift
        ;;
    -i | --interval)
        read_interval="${2}"
        shift
        shift
        ;;
    -* | --*)
        echo "Unknown option $i"
        exit 1
        ;;
    *) ;;
    esac
done

if [[ -z "${mxl_domain}" ]] || [[ -z "${mxl_flow_id}" ]]; then
    echo "usage: ${program_name} -d <DOMAIN> -f <FLOW_ID> [-s <INTERVAL>]"
    exit 1
fi

flow_dir="${mxl_domain}/${mxl_flow_id}.mxl-flow"
have_flow=1
read_interval="${read_interval:-"0.1"}"

while true; do
    sleep "${read_interval}"
    if [[ ! -f "${flow_dir}/data" ]]; then
        if [[ "${have_flow}" != 0 ]]; then
            echo "Waiting for flow ${mxl_flow_id} to be created"
            have_flow=0
        fi
        continue
    fi
    have_flow=1
    head_index="$(head -c208 <"${flow_dir}/data" | tail -c8 | od -vtu8 | head -n1 | awk '{print $2}')"
    touch "${flow_dir}/access"
    echo "read flow ${mxl_flow_id} head index ${head_index}"
done
