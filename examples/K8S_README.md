# Kubernetes rollout path: local simulation to AWS EFA

This document captures a practical 2-stage path:

1. Validate deployment and media workflow behavior locally with `kind` (3 workers).
2. Migrate to AWS with EFA for real inter-node RDMA-class performance.

---

## Stage 1 — Local simulation on Docker Desktop + kind (3 workers)

### Goal

Validate orchestration and functionality before cloud migration:

- Pod scheduling and restart behavior
- Reader/writer startup and flow lifecycle
- Service exposure and VLC playback path
- Resource guardrails (to avoid host/WSL lockups)

> Note: `kind` does **not** provide real RDMA/EFA data plane behavior. It is for functional and operational validation.

### Prerequisites

- Docker Desktop running
- `kind`, `kubectl`
- Local images built: `mxl-reader:latest`, `mxl-writer:latest`

### 1) Create a 3-worker kind cluster

Create `kind-3w.yaml`:

```yaml
kind: Cluster
apiVersion: kind.x-k8s.io/v1alpha4
nodes:
  - role: control-plane
  - role: worker
    labels:
      node-role: mxl
  - role: worker
    labels:
      node-role: mxl
  - role: worker
    labels:
      node-role: mxl
```

Create cluster:

```bash
kind create cluster --name mxl --config kind-3w.yaml
```

### 2) Load images into kind

```bash
kind load docker-image mxl-reader:latest --name mxl
kind load docker-image mxl-writer:latest --name mxl
```

### 3) Apply manifests

From the `examples` folder:

```bash
kubectl apply -f kube-deployment.yaml
```

### 4) Validate health and placement

```bash
kubectl get nodes -o wide
kubectl get pods -o wide
kubectl get svc
```

### 5) Run reader sink and test VLC

Run sink inside the reader pod:

```bash
kubectl exec -it deployment/reader-media-function -- /app/mxl-info -d /domain -l
kubectl exec -it deployment/reader-media-function -- /app/mxl-gst-sink -d /domain -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

Get UDP LoadBalancer service:

```bash
kubectl get svc reader-media-service
```

Use VLC URL:

```text
srt://127.0.0.1:5000?mode=caller
```

If localhost routing is not available in your setup, use WSL IP:

```bash
hostname -I
```

Then in VLC:

```text
srt://<WSL_IP>:5000?mode=caller
```

### 6) Keep resource limits enabled

Use limits to prevent local machine freezes (example already applied for reader):

```yaml
resources:
  limits:
    memory: "2Gi"
```

---

## Stage 2 — Migration to AWS + EFA

### Goal

Move from functional validation to true multi-node high-performance fabric behavior.

### What changes from Stage 1

- `kind` simulated cluster → EKS cluster on EFA-capable EC2 instances
- Docker Desktop networking → VPC/subnet/security-group design
- No RDMA path → real EFA/libfabric transport path

### High-level setup

1. Provision EKS with EFA-capable worker nodes.
2. Install and validate EFA software stack on nodes.
3. Deploy required Kubernetes device/plugin components for EFA/RDMA exposure to pods.
4. Build/deploy media-function images with required fabric runtime dependencies.
5. Apply manifests with explicit CPU/memory requests+limits and placement constraints.
6. Run the same functional checks from Stage 1.
7. Add performance tests (latency, jitter, throughput, CPU).

### Validation checklist

- Pods can discover and open flows across nodes.
- No packet-loss-like artifacts under load.
- Reader latency/jitter meets expected target.
- CPU usage is lower than non-fabric fallback path.
- Failure/restart behavior remains stable.

---

## Recommended rollout strategy

- Keep the same deployment shape and runbooks between Stage 1 and Stage 2.
- Change one variable class at a time (network/fabric, then scaling, then tuning).
- Record baseline metrics in Stage 1 and compare after EFA migration.

This minimizes migration risk and makes performance regressions easier to diagnose.
