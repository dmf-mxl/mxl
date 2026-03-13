#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Interactive SRT stream selector for the MXL reader container.
# Lists available flows and starts mxl-gst-sink for the selected one.
#
# Usage: ./examples/stream.sh        (interactive flow selection)
#        ./examples/stream.sh stop    (stop active stream)

set -euo pipefail

CONTAINER="reader-media-function"
DOMAIN="/domain"

# Handle 'stop' subcommand
if [[ "${1:-}" == "stop" ]]; then
  if podman exec "$CONTAINER" pgrep -f mxl-gst-sink >/dev/null 2>&1; then
    podman exec "$CONTAINER" pkill -f mxl-gst-sink || true
    echo "⏹️  Stopped active stream in '${CONTAINER}'."
  else
    echo "ℹ️  No active stream in '${CONTAINER}'."
  fi
  exit 0
fi

# Check if reader container is running
if ! podman ps --format '{{.Names}}' | grep -q "^${CONTAINER}$"; then
  echo "❌ Container '${CONTAINER}' is not running."
  echo "   Start it first: podman-compose -f examples/podman-compose.yaml --profile test-source up"
  exit 1
fi

# Check if mxl-gst-sink is already running inside the container
if podman exec "$CONTAINER" pgrep -f mxl-gst-sink >/dev/null 2>&1; then
  echo "⚠️  mxl-gst-sink is already running in '${CONTAINER}'."
  read -rp "   Kill it and start a new stream? [y/N] " answer
  if [[ "${answer,,}" == "y" ]]; then
    podman exec "$CONTAINER" pkill -f mxl-gst-sink || true
    sleep 1
  else
    echo "Aborted."
    exit 0
  fi
fi

# List available flows
echo "📡 Available MXL flows:"
echo ""

flows=$(podman exec "$CONTAINER" /app/mxl-info -d "$DOMAIN" -l 2>/dev/null)

if [[ -z "$flows" ]]; then
  echo "❌ No flows found. Is a writer running?"
  exit 1
fi

# Parse flows into arrays
i=1
declare -a uuids
declare -a types
declare -a descriptions
while IFS= read -r line; do
  uuid=$(echo "$line" | cut -d',' -f1 | tr -d ' ')
  # Extract the flow name (first quoted string after UUID)
  flow_name=$(echo "$line" | sed 's/^[^"]*"\([^"]*\)".*/\1/')
  desc=$(echo "$line" | cut -d',' -f2- | tr -d '"')
  # Detect type from flow name
  if [[ "$flow_name" == Video* ]]; then
    type="video"
    label="🎬 Video"
  else
    type="audio"
    label="🔊 Audio"
  fi
  uuids+=("$uuid")
  types+=("$type")
  descriptions+=("$desc")
  echo "  [$i] $label  $uuid —$desc"
  ((i++))
done <<< "$flows"

echo ""

# Check if any video flows exist
has_video=false
for t in "${types[@]}"; do
  [[ "$t" == "video" ]] && has_video=true && break
done

if ! $has_video; then
  echo "⚠️  No video flows found. Audio-only flows cannot be streamed via SRT to VLC."
  exit 0
fi

read -rp "Select flow [1-${#uuids[@]}]: " choice

if [[ ! "$choice" =~ ^[0-9]+$ ]] || (( choice < 1 || choice > ${#uuids[@]} )); then
  echo "❌ Invalid selection."
  exit 1
fi

selected_uuid="${uuids[$((choice-1))]}"
selected_type="${types[$((choice-1))]}"

if [[ "$selected_type" == "video" ]]; then
  flag="-v"
else
  flag="-a"
fi

echo ""
echo "▶️  Starting SRT $selected_type stream for: $selected_uuid"
echo "   Open VLC → Network → srt://127.0.0.1:5000?mode=caller"
echo "   Press Ctrl+C to stop."
echo ""

podman exec "$CONTAINER" /app/mxl-gst-sink -d "$DOMAIN" "$flag" "$selected_uuid"
