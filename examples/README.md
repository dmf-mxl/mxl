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

