<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# EFA Cross-Node Setup Guide

This guide explains how to build and deploy MXL with **EFA (Elastic Fabric Adapter)** support for cross-node media transport on AWS EKS.

## Architecture

```
Node A (writer):                          Node B (reader):
┌─────────────────────┐                   ┌──────────────────────────┐
│ mxl-gst-testsrc     │                   │ mxl-fabrics-demo         │
│   ↓ writes grains   │                   │   (target mode)          │
│ /domain (tmpfs)     │  ── EFA/RDMA ──>  │   ↓ receives grains     │
│   ↓ reads grains    │                   │ /domain (tmpfs)          │
│ mxl-fabrics-demo    │                   │   ↓ reads grains         │
│   (initiator mode)  │                   │ mxl-gst-sink → SRT:5000 │
└─────────────────────┘                   └──────────────────────────┘
                                                    ↓
                                              NLB (UDP 5000)
                                                    ↓
                                              VLC player
```

**Writer pod**: generates test video+audio flows → pushes grains over EFA to reader.
**Reader pod**: receives grains over EFA → outputs SRT stream on port 5000.
**Target-info exchange**: reader exposes a tiny HTTP endpoint (busybox sidecar) so the writer can discover it via a headless K8s Service (`reader-fabrics-svc`).

## Prerequisites

- EKS cluster with EFA-capable nodes (e.g. `c5n.9xlarge`) — provisioned via Terraform (`mxl-dmf-terraform`)
- `enable_efa_support = true` and `enable_nlb = true` in `terraform.tfvars`
- AWS EFA K8s device plugin installed (done by Terraform's `helm_release.aws_efa_k8s_device_plugin`)
- At least 2 nodes in the node group (`node_desired_size = 2`)
- Docker installed locally for building images
- AWS CLI configured with profile `dmf-demo`

## Environment Variables

```bash
export AWS_REGION=eu-central-1
export AWS_ACCOUNT_ID=720180693591
export AWS_PROFILE=dmf-demo

aws sso login
aws sts get-caller-identity
```

## Step 1: Build libfabric from Source

The Debian `libfabric1` package does **not** include the EFA provider. You must build libfabric v2.2.0 (or later) from source with EFA support.

```bash
# Install build dependencies
sudo apt-get install -y build-essential libtool autoconf automake \
    libibverbs-dev librdmacm-dev

# Build libfabric
cd /tmp
git clone --depth 1 --branch v2.2.0 https://github.com/ofiwg/libfabric.git
cd libfabric
./autogen.sh
./configure --prefix=/usr
make -j$(nproc)
sudo make install
sudo ldconfig

# Verify EFA provider is available
fi_info -l | grep efa
```

## Step 2: Build MXL with Fabrics Enabled

The fabrics library and `mxl-fabrics-demo` tool are only built when `MXL_ENABLE_FABRICS_OFI=ON`.

```bash
cd /path/to/mxl-dmf-demo

# Configure with fabrics enabled
cmake --preset Linux-Clang-Release -DMXL_ENABLE_FABRICS_OFI=ON

# Build
ninja -C build/Linux-Clang-Release all
```

Verify the fabrics artifacts exist:

```bash
ls build/Linux-Clang-Release/tools/mxl-fabrics-demo/mxl-fabrics-demo
ls build/Linux-Clang-Release/lib/fabrics/ofi/libmxl-fabrics*
```

## Step 3: Build EFA Docker Images

Two new Dockerfiles are provided that include the fabrics library and bridge tool:

- `examples/Dockerfile.writer.efa.txt` — writer + fabrics initiator
- `examples/Dockerfile.reader.efa.txt` — reader + fabrics target + SRT sink

The Docker images bundle the locally-built libfabric (with EFA provider) rather than the Debian package. Before building, stage the library into the build directory:

```bash
mkdir -p build/Linux-Clang-Release/lib/fabrics/ofi/deps
cp /usr/lib/libfabric.so.1.28.0 build/Linux-Clang-Release/lib/fabrics/ofi/deps/
```

Then build the images:

```bash
docker build -f examples/Dockerfile.writer.efa.txt -t mxl-writer-efa:latest .
docker build -f examples/Dockerfile.reader.efa.txt -t mxl-reader-efa:latest .
```

The images also install `librdmacm1t64` and `libibverbs1` from Debian, which are runtime dependencies of the EFA provider.

> **Note**: These are separate from the original `mxl-writer` / `mxl-reader` images which remain unchanged.

## Step 4: Create ECR Repositories and Push Images

```bash
# Create repositories (idempotent)
aws ecr create-repository --repository-name mxl-writer-efa --region $AWS_REGION --profile $AWS_PROFILE || true
aws ecr create-repository --repository-name mxl-reader-efa --region $AWS_REGION --profile $AWS_PROFILE || true

# Login to ECR
aws ecr get-login-password --region $AWS_REGION --profile $AWS_PROFILE \
  | docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com

# Tag and push
docker tag mxl-writer-efa:latest ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer-efa:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer-efa:latest

docker tag mxl-reader-efa:latest ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader-efa:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader-efa:latest
```

## Step 5: Deploy Infrastructure with Terraform

```bash
cd /path/to/mxl-dmf-terraform

terraform init
terraform apply
```

After apply completes, connect kubectl:

```bash
eval "$(terraform output -raw kubeconfig_update_command)"
kubectl get nodes -o wide
```

Verify you have 2 nodes and that EFA device plugin pods are running:

```bash
kubectl get pods -n kube-system | grep efa
```

## Step 6: Deploy the Manifest

```bash
cd /path/to/mxl-dmf-demo
kubectl apply -f examples/aws/kube-aws-2-nodes-efa.yaml
```

Check deployment:

```bash
kubectl get pods -o wide
kubectl get svc
```

Verify:
- Writer and reader pods are on **different nodes** (anti-affinity)
- Both pods are `Running`
- `reader-fabrics-svc` headless service is listed

## Step 7: Verify EFA Transport

Check reader logs (should show target is ready and receiving grains):

```bash
kubectl logs deployment/reader-media-function-efa -c reader-container
```

Check writer logs (should show testsrc producing + initiator transferring):

```bash
kubectl logs deployment/writer-media-function-efa
```

Check target-info exchange:

```bash
kubectl logs deployment/reader-media-function-efa -c target-info-server
```

## Step 8: Play SRT Stream in VLC

Get the NLB hostname:

```bash
# From Terraform
cd /path/to/mxl-dmf-terraform
terraform output reader_nlb_hostname

# Or from kubectl
kubectl get svc reader-media-service -o jsonpath='{.status.loadBalancer.ingress[0].hostname}'
```

Open VLC → Media → Open Network Stream:

```
srt://<NLB_HOSTNAME>:5000?mode=caller
```

## Troubleshooting

### Pods stuck in Pending

```bash
kubectl describe pod <pod-name>
```

Common causes:
- Not enough EFA resources — check `vpc.amazonaws.com/efa` capacity with `kubectl describe node`
- Anti-affinity can't be satisfied — need at least 2 nodes

### Rolling updates stuck (EFA resource contention)

Each node has a limited number of EFA interfaces. During a rolling update the old pod holds the EFA resource, so the new pod stays `Pending`. Force the update by scaling down the old ReplicaSet:

```bash
# Find the old ReplicaSet (READY=1) and new one (READY=0)
kubectl get rs

# Scale down the old RS to free the EFA resource
kubectl scale rs <old-rs-name> --replicas=0

# The new pod should now schedule and start
kubectl get pods -w
```

### Writer can't reach reader-fabrics-svc

```bash
kubectl exec deployment/writer-media-function-efa -- curl -s http://reader-fabrics-svc:8080/target-info.txt
```

If this fails, check that the headless service and reader sidecar are running:

```bash
kubectl get svc reader-fabrics-svc
kubectl get endpoints reader-fabrics-svc
kubectl logs deployment/reader-media-function-efa -c target-info-server
```

### "Failed to parse provider 'efa'" or missing libfabric

This means the Docker image is using a libfabric without the EFA provider. Verify the bundled libfabric is the locally-built one (not the Debian package):

```bash
kubectl exec deployment/reader-media-function-efa -c reader-container -- \
  ls -la /app/libfabric.so.1
# Should point to libfabric.so.1.28.0 (built from source)
```

If it links to a different version, re-stage and rebuild the Docker images (see Step 3).

### "cannot open shared object file: librdmacm.so.1"

The RDMA libraries are missing. Ensure `librdmacm1t64` and `libibverbs1` are installed in the Dockerfile, and that `mxl-fabrics-demo` has the extended rpath:

```bash
kubectl exec deployment/reader-media-function-efa -c reader-container -- \
  ldd /app/mxl-fabrics-demo | grep rdmacm
```

### No grains transferred

Check EFA device availability on both nodes:

```bash
kubectl exec deployment/writer-media-function-efa -- ls /dev/infiniband/
kubectl exec deployment/reader-media-function-efa -c reader-container -- ls /dev/infiniband/
```

### Reader has no video output

Verify grains are arriving:

```bash
kubectl exec deployment/reader-media-function-efa -c reader-container -- /app/mxl-info -d /domain -l
```

If flows are listed but SRT isn't working, check GStreamer:

```bash
kubectl logs deployment/reader-media-function-efa -c reader-container | grep -i srt
```

## Cleanup

```bash
kubectl delete -f examples/aws/kube-aws-2-nodes-efa.yaml

# To destroy all infrastructure:
cd /path/to/mxl-dmf-terraform
terraform destroy
```

Optional ECR cleanup:

```bash
aws ecr delete-repository --repository-name mxl-writer-efa --force --region $AWS_REGION --profile $AWS_PROFILE
aws ecr delete-repository --repository-name mxl-reader-efa --force --region $AWS_REGION --profile $AWS_PROFILE
```

## Files Reference

| File | Purpose |
|---|---|
| `examples/Dockerfile.writer.efa.txt` | Writer image with fabrics initiator |
| `examples/Dockerfile.reader.efa.txt` | Reader image with fabrics target + SRT sink |
| `examples/aws/entrypoint-writer-efa.sh` | Writer entrypoint: testsrc + fetch target-info + initiator |
| `examples/aws/entrypoint-reader-efa.sh` | Reader entrypoint: target + write target-info + gst-sink |
| `examples/aws/kube-aws-2-nodes-efa.yaml` | K8s manifest: 2 deployments + headless service |
