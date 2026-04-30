<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Kubernetes on AWS (EKS) Setup Guide

This guide explains how to publish `examples/kube-deployment-1-node.yaml` to AWS EKS in region `eu-central-1` using account `720180693591` with a cost-limited setup.

## 0) Inputs used in this guide

- AWS Region: `eu-central-1`
- AWS Account ID: `720180693591`
- AWS username/email: `p.ardalani@qvest-digital.com`
- Cluster name (suggested): `mxl-demo-eks`
- Node group (suggested): `ng-1`

## 1) Prerequisites

Install these tools locally:

- `aws` CLI v2
- `kubectl`
- `eksctl`
- `docker`

Verify:

```bash
aws --version
kubectl version --client
eksctl version
docker --version
```

## 2) Configure AWS credentials

Use AWS IAM Identity Center (SSO):

```bash
aws configure sso --profile dmf-demo
export AWS_PROFILE=dmf-demo
```

Use these values during setup:

- SSO session name: `qvest-sso`
- SSO start URL: `https://qvest-digital.awsapps.com/start/#`
- SSO region: `eu-central-1`
- CLI default region: `eu-central-1`
- CLI output format: `json`

Sign in and validate identity:

```bash
aws sso login --profile dmf-demo
#Click on the second link and login

aws sts get-caller-identity --profile dmf-demo
```


## 3) Export environment variables

```bash
export AWS_REGION=eu-central-1
export AWS_ACCOUNT_ID=720180693591
export CLUSTER_NAME=mxl-demo-eks
export AWS_PROFILE=dmf-demo
```

## 4) Create ECR repositories and push required images

The YAML currently requires these images:

- `mxl-writer:latest`
- `mxl-reader:latest`

Create ECR repositories:

```bash
aws ecr create-repository --repository-name mxl-writer --region $AWS_REGION || true
aws ecr create-repository --repository-name mxl-reader --region $AWS_REGION || true
```

Login Docker to ECR:

```bash
aws ecr get-login-password --region $AWS_REGION \
  | docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com
```

Tag and push images:

```bash
docker tag mxl-writer:latest ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer:latest

docker tag mxl-reader:latest ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader:latest
```

Update image values in `examples/kube-deployment-1-node.yaml`:

- `mxl-writer:latest` -> `${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer:latest`
- `mxl-reader:latest` -> `${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader:latest`

## 5) Create a minimal-cost EKS cluster (1 node)

```bash
eksctl create cluster \
  --name $CLUSTER_NAME \
  --region $AWS_REGION \
  --managed \
  --nodegroup-name ng-1 \
  --node-type t3.large \
  --nodes 1 \
  --nodes-min 1 \
  --nodes-max 1 \
  --version 1.30

eksctl create nodegroup \
  --cluster $CLUSTER_NAME \
  --region $AWS_REGION \
  --name ng-2 \
  --node-type t3.large \
  --nodes 1 \
  --nodes-min 1 \
  --nodes-max 1 \
  --managed

eksctl delete cluster \
  --name $CLUSTER_NAME \
  --region $AWS_REGION \
  --wait
```

Notes for cost control:

- Keep nodes fixed at `1/1/1`
- Use one service of type `LoadBalancer` only when needed
- Delete the cluster when not in use

## 6) Connect kubectl to EKS

```bash
# If your cluster was created with Terraform (recommended):
# cd terraform && eval "$(terraform output -raw kubeconfig_update_command)" && cd -

# Manual fallback:
aws eks update-kubeconfig --region $AWS_REGION --name $CLUSTER_NAME --alias $CLUSTER_NAME --profile $AWS_PROFILE
kubectl get nodes
```

## 7) Make the current YAML schedulable on EKS

Your file has `nodeSelector: role: worker`. EKS nodes usually do **not** have this label by default.

### Option A (recommended): remove the nodeSelector

Remove this block from both Deployments in `examples/kube-deployment-1-node.yaml`:

```yaml
nodeSelector:
  role: worker
```

### Option B: keep selector and label the node

```bash
kubectl get nodes
kubectl label node <node-name> role=worker
kubectl get nodes --show-labels | grep role=worker
```

For a single-node cluster, you can label automatically:

```bash
kubectl label node "$(kubectl get nodes -o name | head -n1 | cut -d/ -f2)" role=worker --overwrite
```

## 8) Apply manifest

```bash
kubectl apply -f examples/aws/kube-aws-1-node.yaml

kubectl logs -f deploy/reader-media-function
```

`reader-media-function` starts `/app/mxl-gst-sink` automatically from the Deployment command.

Check rollout:

```bash
kubectl get pods
kubectl get deploy
kubectl get svc
```

## 9) Get public endpoint (UDP 5000)

Your service `reader-media-service` is already `type: LoadBalancer` on port UDP `5000`.

Check external address:

```bash
kubectl get svc reader-media-service -o wide
```

Wait until `EXTERNAL-IP` is assigned.

## 10) Troubleshooting

If Pods stay `Pending`:

- Run `kubectl describe pod <pod-name>`
- Confirm node selector mismatch is fixed (Step 7)

If image pull fails:

- Confirm ECR image URLs are in YAML
- Confirm images exist in ECR
- Confirm cluster is in same region (`eu-central-1`)

If PVC does not bind:

- Check `kubectl get pv,pvc`
- This setup uses a static `hostPath` PV and is single-node oriented

## 11) Cleanup to stop charges

```bash
eksctl delete cluster --name $CLUSTER_NAME --region $AWS_REGION
```

Optional ECR cleanup:

```bash
aws ecr delete-repository --repository-name mxl-writer --force --region $AWS_REGION
aws ecr delete-repository --repository-name mxl-reader --force --region $AWS_REGION
```
