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

The CUDA payload tests are a separate image. From the repository root:

```bash
docker build -f examples/Dockerfile.cuda-tests -t mxl-cuda-tests .
docker run --rm --gpus all --shm-size=1g mxl-cuda-tests
```

`--gpus all` makes every GPU visible. `--shm-size` gives the tests a `/dev/shm` domain. The image contains the CUDA toolkit only for the build. The machine that runs it needs the NVIDIA driver and the NVIDIA container toolkit. Peer access between two GPUs is a property of that machine. The peer test skips when it is not available.

The same image can run the writer and the reader in two containers. The script bind-mounts one tmpfs directory into both, and that directory is the MXL domain. The payload descriptor travels on a socket in the flow directory, so the containers do not share an IPC namespace or a PID namespace.

```bash
examples/cuda-cross-container-test.sh
```

On a machine that only has the image, copy the script out first:

```bash
docker create --name mxl-cuda-extract mxl-cuda-tests
docker cp mxl-cuda-extract:/opt/mxl/bin/mxl-cuda-cross-container.sh ./mxl-cuda-cross-container.sh
docker rm mxl-cuda-extract
bash ./mxl-cuda-cross-container.sh
```

`mxl-gst-cuda-testsrc` and `mxl-gst-cuda-sink` have their own image. From the repository root:

```bash
docker build -f examples/Dockerfile.cuda-gst -t mxl-cuda-gst .
```

Run the writer and the sink in two containers. `nvidia-smi -L` prints the GPU UUIDs. Mount the same tmpfs directory as the domain in both containers. `--device-uuid` on the sink selects the GPU that accesses the payload. That GPU must have peer access to the writer's GPU. Both containers must see the writer's GPU.

```bash
mkdir -p /dev/shm/mxl
docker run -d --name mxl-cuda-writer --gpus all \
  -v /dev/shm/mxl:/dev/shm/mxl \
  --entrypoint /opt/mxl/bin/mxl-gst-cuda-testsrc mxl-cuda-gst \
  -d /dev/shm/mxl -v /opt/mxl/data/v210_flow.json --device-uuid GPU-<writer>

xhost +local:
docker run --rm --gpus all \
  -v /dev/shm/mxl:/dev/shm/mxl \
  -e DISPLAY -v /tmp/.X11-unix:/tmp/.X11-unix \
  --entrypoint /opt/mxl/bin/mxl-gst-cuda-sink mxl-cuda-gst \
  -d /dev/shm/mxl -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed --device-uuid GPU-<reader>

docker rm -f mxl-cuda-writer
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

# CUDA fabrics between two nodes

Writer and reader on different nodes use separate domains. The writer's domain holds a CUDA payload on the writer's GPU. The target's domain holds a CUDA payload on the target's GPU. The fabrics initiator copies grains from the first domain into the second with RDMA. The domains do not share memory, so CUDA IPC is not used.

```bash
SRC_GPU=0 DST_GPU=0 ./examples/scripts/fabrics-cuda-two-domain.sh
```

The script builds on `mxl-gst-cuda-testsrc`, which needs the `MXL_ENABLE_CUDA_PAYLOAD` build, and `mxl-fabrics-demo`, which needs `MXL_ENABLE_FABRICS_OFI`. `SRC_GPU` and `DST_GPU` are GPU indexes. The script reads their UUIDs with `nvidia-smi`. Run it on the target node for a same-host check, or split the parts across two nodes.

The target needs `nvidia_peermem` loaded. Without it, the verbs memory registration of the device payload fails. The initiator copies each device payload into a staging buffer before the RDMA write.

Kubernetes skeleton: `kube-fabrics-cuda-two-domain.yaml`. It has one pod for the writer and initiator and one for the target, each with one GPU and separate `emptyDir` memory domains. Render it with the hostnames and the GPU UUIDs from `nvidia-smi -L`:

```bash
examples/scripts/render-kube-fabrics-template.sh src-node dst-node GPU-<src> GPU-<dst> > /tmp/mxl-fabrics-cuda.yml
kubectl apply -f /tmp/mxl-fabrics-cuda.yml
# After the target prints "Target info: <base64>":
kubectl create configmap mxl-fabrics-target-info \
  --from-literal=target-info.b64='<paste-base64>' \
  -o yaml --dry-run=client | kubectl apply -f -
```

The skeleton needs an image built with CUDA and `MXL_ENABLE_FABRICS_OFI`. The stock `examples/Dockerfile` does not build either. The pods also need the NVIDIA device plugin, `/dev/infiniband`, and an RDMA fabric. It does not run on a stock k3s node without these.
