#! /bin/bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0

trap "exit 0" SIGINT SIGTERM

program_name="${0}"
while [[ $# -gt 0 ]]; do
    case $1 in
    -d | --domain)
        mxl_domain="${2}"
        shift
        shift
        ;;
    -v | --video-flow-id)
        mxl_video_flow_id="${2}"
        shift
        shift
        ;;
    -a | --audio-flow-id)
        mxl_audio_flow_id="${2}"
        shift
        shift
        ;;
    -i | --interval)
        read_interval="${2}"
        shift
        shift
        ;;
    -*)
        echo "Unknown option $i"
        exit 1
        ;;
    *) ;;
    esac
done

function flow_last_write_time() {
    head -c224 <"${flow_dir}/data" | tail -c8 | od -vtu8 | head -n1 | awk '{print $2}'
}

function flow_head_index() {
    head -c208 <"${flow_dir}/data" | tail -c8 | od -vtu8 | head -n1 | awk '{print $2}'
}

if [[ -z "${mxl_domain}" ]] || [[ -z "${mxl_video_flow_id}" ]] && [[ -z "${mxl_audio_flow_id}" ]]; then
    echo "usage: ${program_name} -d <DOMAIN> (-v <VIDEO_FLOW_ID> | -a <AUDIO_FLOW_ID>) [-i <INTERVAL>]"
    exit 1
fi

is_video=0
if [ ! -z "${mxl_video_flow_id}" ]; then
    flow_id="${mxl_video_flow_id}"
    flow_dir="${mxl_domain}/${mxl_video_flow_id}.mxl-flow"
    is_video=1
else
    flow_id="${mxl_audio_flow_id}"
    flow_dir="${mxl_domain}/${mxl_audio_flow_id}.mxl-flow"
fi

have_flow=1
read_interval="${read_interval:-"0.1"}"

while true; do
    sleep "${read_interval}"
    if [[ ! -f "${flow_dir}/data" ]]; then
        if [[ "${have_flow}" != 0 ]]; then
            echo "Waiting for flow ${flow_id} to be created"
            have_flow=0
        fi
        continue
    fi
    have_flow=1
    touch "${flow_dir}/access"
    if [[ "${is_video}" = "1" ]]; then
        echo "read video flow ${flow_id} head index $(flow_head_index "${flow_id}"), last write: $(flow_last_write_time "${flow_id}")"
    else
        echo "read audio flow ${flow_id} head index $(flow_head_index "${flow_id}")"
    fi
done
