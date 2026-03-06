#!/usr/bin/env bash

set -euo pipefail

if ! command -v terraform >/dev/null 2>&1; then
  echo "terraform is not installed or not in PATH" >&2
  exit 1
fi

if ! command -v kubectl >/dev/null 2>&1; then
  echo "kubectl is not installed or not in PATH" >&2
  exit 1
fi

if ! command -v aws >/dev/null 2>&1; then
  echo "aws CLI is not installed or not in PATH" >&2
  exit 1
fi

echo "Reading kubeconfig command from Terraform output..."
kubeconfig_command="$(terraform output -raw kubeconfig_update_command)"

if [[ -z "$kubeconfig_command" ]]; then
  echo "kubeconfig_update_command output is empty" >&2
  exit 1
fi

echo "Updating kubeconfig..."
eval "$kubeconfig_command"

echo "Current kubectl context:"
kubectl config current-context

echo "Cluster nodes:"
kubectl get nodes
