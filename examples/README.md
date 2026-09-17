<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Docker Compose Example

This example publishes MXL video and audio test flows into a shared [MXL domain](../docs/Architecture.md#shared-memory-model) (a directory of shared-memory flows) and consumes them, using 6 containers: 3 for video and 3 for audio. The images are built from source; pre-built binaries and container images are not published from this repository.

## Quick Start

On Linux, no git checkout is required:

```bash
curl -fsSL https://raw.githubusercontent.com/dmf-mxl/mxl/main/examples/bootstrap.sh | bash -s -- --yes --up
```

The `bootstrap.sh` script is a shortcut for [Building](#building) and [Running](#running) below: it downloads the sources, installs Docker Engine and Compose if they are missing (a `sudo` password prompt is expected), then runs `docker compose build` and `docker compose up`. From a clone, run `./examples/bootstrap.sh --up` instead (add `--yes` to skip confirmation prompts).

It unpacks sources into `./mxl`, which you can override with `MXL_SOURCE_DIR` / `MXL_REF` set on the `bash` process, not on `curl`. Those variables are ignored when you run the script from a clone. The example images are currently x86_64 only.

It can install Docker from distro packages on Ubuntu and Debian (`docker.io`, `docker-compose-v2`), Fedora (`moby-engine`), and Arch Linux (`pacman`). On RHEL, CentOS, Rocky, and AlmaLinux (no distro Docker package) it installs Docker CE. On any other distro, install Docker and Compose yourself, then re-run, or pass `--skip-docker` if Docker is already managed on the machine.

When the stack is up, check that it is working:

```bash
docker logs mxl-example-video-flow-info-1
```

Stop with Ctrl+C (if you used `docker compose up` in the foreground), then from the `examples` directory:

```bash
docker compose down
```

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

> **NOTE:** Out of the box, the setup works correctly only with docker.io. When using Docker CE, `docker compose up` may fail with:
>
> ```
> invalid mount config for type "bind": bind source path does not exist: /dev/shm/mxl
> ``` 

## The Containers

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

## Previewing the Flows on the Host

The steps above only use tools **inside** the containers. Watching or playing the same flows with `mxl-gst-sink` on your machine is optional and separate: you need a **host** build of the SDK and tools ([docs/Building.md](../docs/Building.md)), plus `jq` for the bind script.

Bind the compose MXL domain to a local directory:

```bash
scripts/bind-compose-domain.sh ./mxl-domain
```

Then, with host-built `mxl-gst-sink` on your `PATH`:

```bash
# show video flow
mxl-gst-sink -d ./mxl-domain -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed

# play audio flow
mxl-gst-sink -d ./mxl-domain -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
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
