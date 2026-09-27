<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Docker Compose Example
This examples launches a total of 6 containers, 3 for video, 3 for audio.

```
mxl-example-audio-flow-writer
mxl-example-video-flow-writer
```
These run the `mxl-gst-testsrc` tool provided in the repository to publish a test signal.

```
mxl-example-audio-fake-reader
mxl-example-video-fake-reader
```
These simulate side effects of a reader consuming the respective flows, such as updating the `Last read time` of discrete flows.

```
mxl-example-audio-flow-info
mxl-example-video-flow-info
```

These print out information about the video and audio flow to stdout.
You can check the output and observe if everything is working correctly by running:
```bash
docker logs mxl-example-video-flow-info-1
```

You can also bind the mxl domain created as part of the docker compose deployment to a local mountpoint using a script provided in the `scripts` directory.
```bash
scripts/bind-compose-domain.sh ./mxl-domain
```

You can then preview the flows published by the containerized media functions in your host environment:
```bash
# show video flow
mxl-gst-sink -d ./mxl-domain -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed

# play audio flow
mxl-gst-sink -d ./local-domain -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

> **NOTE:** Out of the box, the setup works correctly only with docker.io. When using Docker CE, `docker compose up` may fail with:
>
> ```
> invalid mount config for type "bind": bind source path does not exist: /dev/shm/mxl
> ``` 

## Building

In the "examples" directory run:

```bash
docker compose build
```
## Running

In the "examples" directory run: 
```bash
docker compose up
```
or to start in the background:
```bash
docker compose up -d
```

# Kubernetes Example

Note: Tested on K3S and Kubernetes created with `kubeadm`. This will probably not run on more restrictive Kubernetes distributions like OpenShift or Rancher without modification.

Follow the same steps above to build the images. If you don't want to use a registry to access the images from the kubernetes cluster, you can export the images to a file and import them on your kubernetes cluster node.
On the system where you built the images:
```bash
scripts/export-images.sh mxl-example-images.tar.gz
```
On the kubernetes node (when using containerd):
```bash
gunzip < mxl-example-images.tar.gz | sudo ctr image import -
```

If you want to use a registry, you will need to change the image references in `kube-example.yaml` to point to the images in your registry.
```bash
sed -i 's%docker.io/library/%images.mycompany.com/repos/%g'
```

Because PersistentVolumes requires a `nodeAffinity` clause you also need to inject the hostname of the node you want to run the example containers on into the deployment.
You can use the provided script.
```bash
scripts/render-kube-template.sh my-node-hostname > /tmp/deployment.yml
```

You can then deploy the resources with:
```bash
kubectl apply -r /tmp/deployment.yml
```

To check the pods that are running use this command:

``` bash
kubectl get pod -w
```

To check if the video flow writer is producing frames:
```bash
kubectl logs -f mxl-video-flow-info-(...)
```

To check if the audio flow writer is producing samples:
```bash
kubectl logs -f mxl-audio-flow-info-(...)
```

# CUDA writer / reader topologies

| Setup | Domain | How grains move |
| --- | --- | --- |
| Two pods, **same node, same GPU** | Shared `hostPath` (e.g. `/dev/shm/...`) | CUDA IPC |
| Two processes, **same node, two GPUs** | Shared domain; both must see both GPUs | CUDA IPC + P2P |
| Two pods, **different nodes** (or exclusive GPUs that cannot P2P) | One domain per pod; do not share | Fabrics RDMA / GPUDirect |

## Same node, same GPU (two pods)

This is the default CUDA-linear path: the writer `cudaMalloc`s, publishes IPC handles under the flow directory, and the reader `cudaIpcOpenMemHandle`s. Do not pass `--device-index` on the reader.

Same-machine rehearsal:

```bash
./examples/scripts/cuda-ipc-same-gpu.sh
```

Kubernetes skeleton: `kube-cuda-ipc-two-pod.yaml`. Both pods must land on one node, mount the **same** host tmpfs domain, set `hostIPC: true`, and actually share the GPU (device-plugin time-slicing or MPS). Exclusive `nvidia.com/gpu: 1` without sharing either leaves a pod Pending or assigns two different GPUs — that is not CUDA IPC.

```bash
examples/scripts/render-kube-cuda-ipc-template.sh my-node > /tmp/mxl-cuda-ipc.yml
kubectl apply -f /tmp/mxl-cuda-ipc.yml
```

## Same node, two GPUs (P2P)

A reader on the **same node** can map those grains onto a **second GPU** with CUDA IPC peer access (NVLink or P2P-capable PCIe) — no second domain and no fabrics.

```bash
SRC_GPU=0 DST_GPU=1 ./examples/scripts/cuda-p2p-two-gpu.sh
```

`mxl-gst-sink --device-index 1` (or `mxlCreateFlowReader(..., "{\"deviceIndex\":1}", ...)`) selects the local GPU. Kubernetes: `kube-cuda-p2p-two-gpu.yaml` is one pod with **two** GPUs so both processes see device 0 and 1. Two pods that each get a single GPU cannot use this path (`CUDA_VISIBLE_DEVICES` hides the peer).

Pods on different nodes must not share the domain. That path is two local domains plus fabrics RDMA.

