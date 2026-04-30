#!/bin/bash
# SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Build all 3 EFA images locally in the correct order.
# Must be run from the repo root directory.
set -e

echo "==> [1/3] Building base image (mxl stage)..."
docker build \
  --target mxl \
  -t ghcr.io/qvest-digital/mxl-base:local \
  -f examples/Dockerfile.efa.base \
  .

echo "==> [2/3] Building writer image..."
docker build \
  --build-arg BASE_TAG=local \
  -f examples/Dockerfile.efa.writer \
  -t mxl-writer:local \
  .

echo "==> [3/3] Building reader image..."
docker build \
  --build-arg BASE_TAG=local \
  -f examples/Dockerfile.efa.reader \
  -t mxl-reader:local \
  .

echo ""
echo "All 3 EFA images built successfully."
echo "Run with: docker compose -f examples/docker-compose.efa.local.yaml up"
