<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Docker Compose Example

This example launches two containers simulating two different media functions executing in separate containers.  It relies on docker.io and docker-compose-plugin being installed on the host.

- An init container is pre-run to create the 'mxl' folder in the host /dev/shm
- Writer container: A bind mount maps /dev/shm/mxl from the host to /domain  in read-write mode in the writer media function container.  The writer process generates a test pattern with burnt in time of day.
- Reader container: A bind mount maps /dev/shm/mxl from the host to /domain in read-only mode in the reader media function. At the moment the reader media function container is idling.  To use it you need to exec into it:

    ```bash
    docker exec -it examples-reader-media-function-1 /app/mxl-info -d /domain -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed
    ```

> **NOTE:** Out of the box, the setup works correctly only with docker.io. When using Docker CE, `docker compose up` may fail with:
>
> ```
> invalid mount config for type "bind": bind source path does not exist: /dev/shm/mxl
> ``` 

## Building

Compile the whole MXL library and tools using the ```Linux-Clang-Release``` preset by invoking ```ninja all``` in the ```build/Linux-Clang-Release```  directory.  Then, in the "examples" directory, run:

```bash
docker compose --profile '*' build
```

### Profiles

The examples are grouped into Docker Compose profiles. Using `--profile '*'` builds everything, but you can also build only a subset:
- `--profile test-source`: builds the `writer-media-function` example
- `--profile looping-file-source`: builds the `writer-looper-video-media-function` and `writer-looper-audio-media-function` examples

```bash
docker compose --profile test-source build
docker compose --profile looping-file-source build
```

## Running

In the "examples" directory run: 

```bash
docker compose --profile '*' up
```

to run all examples, or run a smaller set by selecting a profile:

```bash
docker compose --profile test-source up
docker compose --profile looping-file-source up
```

# Kubernetes Example

Note: Tested on Docker Desktop Environment which as the correct default storageclass "HostPath" which has a host path provisioner. Therefore its unlikely to work in managed k8s environments without changes

Follow the same steps above to build the container images and then run this command to deploy the kubernetes resources:

```bash
kubectl create -f kube-deployment.yaml
```

To check the pods are running use this command, once both the reader and writer are running, grab the name name of the reader pod:

``` bash
kubectl get pod -w
```

Now execute the following command

``` bash
kubectl exec -it <reader-pod-name> -- /app/mxl-info -d /domain -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```
