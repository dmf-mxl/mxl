#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# This script runs two pipelines to test outputting video and SRT captions on
# separate MXL flows (the producer), and reading them back and displaying the
# video with the captions (the consumer).
#
# Usage:
#   ./scripts/gst-mxl-closedcaptions.sh [SRT_FILE]
#
# Environment (optional):
#   MXL_REPO_ROOT         Repo root (default: inferred from script path).
#   MXL_BUILD_PRESET      CMake preset under build/ (default: Linux-Clang-Release;
#                         falls back to Linux-Clang-Debug if Release libs missing).
#   CARGO_PROFILE         release or debug (default: release).
#   MXL_DOMAIN            MXL domain directory. If unset, a unique subdirectory is
#                         created under /dev/shm when writable, else under TMPDIR,
#                         and removed on exit. If set, it must already exist; the
#                         script does not remove it on exit.
#   MXL_TEST_TIMEOUT_SEC  If set, after this many seconds SIGTERM is sent to the
#                         consumer process.
#
# Requires: GStreamer, gst-plugins-base/bad/good, rsclosedcaption (cctost2038anc,
# st2038anctocc, tttocea608), mxlsink/mxlsrc plugin built, libmxl on LD_LIBRARY_PATH.

set -euo pipefail

usage() {
  sed -n '3,25p' "$0"
  exit "${1:-0}"
}

[[ "${1:-}" == "-h" || "${1:-}" == "--help" ]] && usage 0

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MXL_REPO_ROOT=${MXL_REPO_ROOT:-$(cd "$SCRIPT_DIR/../../.." && pwd)}

PRESET_CANDIDATES=("${MXL_BUILD_PRESET:-Linux-Clang-Release}" Linux-Clang-Release Linux-Clang-Debug)
MXL_BUILD_PRESET=""
for p in "${PRESET_CANDIDATES[@]}"; do
  if [[ -d "$MXL_REPO_ROOT/build/$p/lib" ]]; then
    MXL_BUILD_PRESET=$p
    break
  fi
done
if [[ -z "$MXL_BUILD_PRESET" ]]; then
  echo "error: no build preset found under $MXL_REPO_ROOT/build (tried ${PRESET_CANDIDATES[*]})" >&2
  exit 1
fi

CARGO_PROFILE=${CARGO_PROFILE:-release}
GST_PLUGIN_DIR="$MXL_REPO_ROOT/rust/target/$CARGO_PROFILE"
MXL_LIB_DIR="$MXL_REPO_ROOT/build/$MXL_BUILD_PRESET/lib"
MXL_LIB_INTERNAL="$MXL_REPO_ROOT/build/$MXL_BUILD_PRESET/lib/internal"

for need in "$GST_PLUGIN_DIR" "$MXL_LIB_DIR"; do
  if [[ ! -d "$need" ]]; then
    echo "error: missing directory: $need" >&2
    exit 1
  fi
done

export LD_LIBRARY_PATH="${MXL_LIB_DIR}${MXL_LIB_INTERNAL:+:${MXL_LIB_INTERNAL}}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export GST_PLUGIN_PATH="${GST_PLUGIN_DIR}${GST_PLUGIN_PATH:+:${GST_PLUGIN_PATH}}"

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
  echo "error: gst-launch-1.0 not in PATH" >&2
  exit 1
fi

for el in mxlsink mxlsrc cctost2038anc tttocea608; do
  if ! gst-inspect-1.0 "$el" >/dev/null 2>&1; then
    echo "error: gst-inspect-1.0 $el failed (plugin or element missing)" >&2
    exit 1
  fi
done

if [[ ! -f "$MXL_LIB_DIR/libmxl.so" ]] && [[ ! -f "$MXL_LIB_DIR/libmxl.so.0" ]]; then
  echo "warning: libmxl.so not seen in $MXL_LIB_DIR (LD_LIBRARY_PATH may still work if installed elsewhere)" >&2
fi
if ! compgen -G "$GST_PLUGIN_DIR/libgstmxl.so"* >/dev/null; then
  echo "error: libgstmxl.so not found under $GST_PLUGIN_DIR (build with: cargo build -p gst-mxl-rs)" >&2
  exit 1
fi

for el in cccombiner cea608overlay st2038anctocc ccconverter; do
  if ! gst-inspect-1.0 "$el" >/dev/null 2>&1; then
    echo "error: gst-inspect-1.0 $el failed (plugin or element missing)" >&2
    exit 1
  fi
done

created_srt=false
if [[ "${1:-}" ]]; then
  export SRT_FILE=$1
  if [[ ! -f "$SRT_FILE" ]]; then
    echo "error: SRT file not found: $SRT_FILE" >&2
    exit 1
  fi
else
  created_srt=true
  SRT_FILE=$(mktemp "${TMPDIR:-/tmp}/gst-mxl-closedcaptions-cues.XXXXXX.srt")
  export SRT_FILE
  # One cue per second for 30s so burn-in visibly steps (Caption 1, Caption 2, ...).
  cues=30
  srt_ts() {
    # $1 = offset in whole seconds → SRT timecode HH:MM:SS,000
    local t=$1
    printf '%02d:%02d:%02d,000' \
      $((t / 3600)) $(((t / 60) % 60)) $((t % 60))
  }
  for ((i = 0; i < cues; i++)); do
    n=$((i + 1))
    printf '%d\n%s --> %s\nCaption %d\n\n' \
      "$n" "$(srt_ts "$i")" "$(srt_ts "$n")" "$n"
  done >"$SRT_FILE"
  unset cues
fi

domain_managed=false
if [[ -n "${MXL_DOMAIN:-}" ]]; then
  domain_dir=$MXL_DOMAIN
else
  if [[ -d /dev/shm && -w /dev/shm ]]; then
    domain_dir=$(mktemp -d /dev/shm/gst-mxl-closedcaptions.XXXXXX)
  else
    domain_dir=$(mktemp -d "${TMPDIR:-/tmp}/gst-mxl-closedcaptions.XXXXXX")
  fi
  export MXL_DOMAIN=$domain_dir
  domain_managed=true
fi

VIDEO_FLOW_ID=$(uuidgen 2>/dev/null || cat /proc/sys/kernel/random/uuid)
DATA_FLOW_ID=$(uuidgen 2>/dev/null || cat /proc/sys/kernel/random/uuid)
export VIDEO_FLOW_ID DATA_FLOW_ID

producer_pid=
consumer_pid=
stop_producer() {
  if [[ -n "${producer_pid:-}" ]]; then
    kill "$producer_pid" 2>/dev/null || true
    wait "$producer_pid" 2>/dev/null || true
    producer_pid=
  fi
}

stop_consumer() {
  if [[ -n "${consumer_pid:-}" ]]; then
    kill "$consumer_pid" 2>/dev/null || true
    wait "$consumer_pid" 2>/dev/null || true
    consumer_pid=
  fi
}

cleanup() {
  stop_consumer
  stop_producer
  if [[ "$domain_managed" == true ]]; then
    rm -rf "$domain_dir"
  fi
  if [[ "${created_srt:-false}" == true ]] && [[ -f "${SRT_FILE:-}" ]]; then
    rm -f "$SRT_FILE"
  fi
}
trap cleanup EXIT INT TERM

echo "MXL_REPO_ROOT=$MXL_REPO_ROOT"
echo "MXL_BUILD_PRESET=$MXL_BUILD_PRESET CARGO_PROFILE=$CARGO_PROFILE"
echo "MXL_DOMAIN=$MXL_DOMAIN"
echo "VIDEO_FLOW_ID=$VIDEO_FLOW_ID"
echo "DATA_FLOW_ID=$DATA_FLOW_ID"
echo "SRT_FILE=$SRT_FILE"
echo
producer=(gst-launch-1.0 -q \
  videotestsrc is-live=true \
    ! video/x-raw,width=640,height=360,framerate=30000/1001 \
    ! videoconvert \
    ! video/x-raw,format=v210,width=640,height=360,framerate=30000/1001 \
    ! queue \
    ! mxlsink flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" \
  filesrc location="$SRT_FILE" \
    ! subparse \
    ! tttocea608 \
    ! ccconverter \
    ! closedcaption/x-cea-608,framerate=30000/1001 \
    ! cctost2038anc \
    ! meta/x-st-2038,alignment=frame,framerate=30000/1001 \
    ! queue \
    ! mxlsink flow-id="$DATA_FLOW_ID" domain="$MXL_DOMAIN")

consumer=(gst-launch-1.0 -e \
  mxlsrc video-flow-id="$VIDEO_FLOW_ID" domain="$MXL_DOMAIN" \
    ! queue min-threshold-buffers=2 \
    ! videoconvert \
    ! video/x-raw,format=I420,width=640,height=360,framerate=30000/1001 \
    ! queue \
    ! cccombiner name=cc \
    ! cea608overlay \
    ! videoconvert \
    ! autovideosink \
  mxlsrc data-flow-id="$DATA_FLOW_ID" domain="$MXL_DOMAIN" \
    ! queue \
    ! st2038anctocc \
    ! ccconverter \
    ! closedcaption/x-cea-608,format=raw,framerate=30000/1001 \
    ! cc.caption)

echo "Starting consumer in background (mxlsrc waits until flows exist; Ctrl+C here stops both pipelines)…"
"${consumer[@]}" &
consumer_pid=$!

echo "Starting producer in background…"
"${producer[@]}" &
producer_pid=$!

watchdog_pid=
if [[ -n "${MXL_TEST_TIMEOUT_SEC:-}" ]]; then
  (sleep "$MXL_TEST_TIMEOUT_SEC" && kill -TERM "$consumer_pid" 2>/dev/null) &
  watchdog_pid=$!
fi

wait "$consumer_pid"

if [[ -n "${watchdog_pid:-}" ]]; then
  kill "$watchdog_pid" 2>/dev/null || true
  wait "$watchdog_pid" 2>/dev/null || true
fi

if [[ -n "${MXL_TEST_TIMEOUT_SEC:-}" ]]; then
  echo "MXL_TEST_TIMEOUT_SEC=${MXL_TEST_TIMEOUT_SEC} (watchdog may have SIGTERM'd the consumer if it was still running)."
fi
