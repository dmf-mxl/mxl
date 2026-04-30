## 1. Prerequisites & Environment

Ensure your local machine has the following installed:

- **aws CLI v2**
- **kubectl**
- **eksctl**
- **terraform**
- **docker**

**Set Environment Variables**

```bash
export AWS_REGION=eu-central-1
export AWS_ACCOUNT_ID=720180693591
export CLUSTER_NAME=mxl-demo-eks
export AWS_PROFILE=dmf-demo

aws sso login --profile dmf-demo
aws sts get-caller-identity --profile dmf-demo
```

---

## 2. Build libfabric & MXL with EFA Support

The standard Debian libfabric does not include the EFA provider. You must build it from source.

### Build libfabric v2.2.0+

```bash
sudo apt-get install -y build-essential libtool autoconf automake libibverbs-dev librdmacm-dev

cd /tmp
git clone --depth 1 --branch v2.2.0 https://github.com/ofiwg/libfabric.git
cd libfabric && ./autogen.sh && ./configure --prefix=/usr
make -j$(nproc) && sudo make install && sudo ldconfig

# Verify EFA is listed
fi_info -l | grep efa
```

### Build MXL (Fabrics Enabled)

```bash
cd /path/to/mxl-dmf-demo
cmake --preset Linux-Clang-Release -DMXL_ENABLE_FABRICS_OFI=ON
ninja -C build/Linux-Clang-Release all
```

---

## 3. Prepare & Push EFA Docker Images

You must bundle the locally built libfabric into the EFA-specific Docker images.

### Stage Library and Build

```bash
mkdir -p build/Linux-Clang-Release/lib/fabrics/ofi/deps
cp /usr/lib/libfabric.so.1.28.0 build/Linux-Clang-Release/lib/fabrics/ofi/deps/

docker build -f examples/Dockerfile.writer.efa.txt -t mxl-writer-efa:latest .
docker build -f examples/Dockerfile.reader.efa.txt -t mxl-reader-efa:latest .
```

### Push to Amazon ECR

```bash
aws ecr create-repository --repository-name mxl-writer-efa --region $AWS_REGION || true
aws ecr create-repository --repository-name mxl-reader-efa --region $AWS_REGION || true

aws ecr get-login-password --region $AWS_REGION | docker login --username AWS --password-stdin ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com

docker tag mxl-writer-efa:latest ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer-efa:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-writer-efa:latest

docker tag mxl-reader-efa:latest ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader-efa:latest
docker push ${AWS_ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com/mxl-reader-efa:latest
```

---

## 4. Provision Infrastructure

EFA requires specific instance types (e.g., `c5n.9xlarge`) and the AWS EFA K8s device plugin.

### Terraform Deployment

Navigate to your Terraform directory.

Ensure `enable_efa_support = true` and `node_desired_size = 2` are set in `terraform.tfvars`.

Run:

```bash
terraform init && terraform apply
eval "$(terraform output -raw kubeconfig_update_command)"
```

### Verify EFA Resources

```bash
kubectl get pods -n kube-system | grep efa
kubectl describe node <node-name> | grep vpc.amazonaws.com/efa
```

---

## 5. Deploy & Verify MXL

Deploy the EFA-enabled manifest which handles the Writer (Initiator) and Reader (Target) logic.

### Apply Manifest

```bash
kubectl apply -f examples/aws/kube-aws-2-nodes-efa.yaml
```

### Monitoring & Output

- **Check Reader Logs:**
	```bash
	kubectl logs deployment/reader-media-function-efa -c reader-container
	```
- **Check Writer Logs:**
	```bash
	kubectl logs deployment/writer-media-function-efa
	```
- **Stream with VLC:**
	Get the NLB hostname via `kubectl get svc reader-media-service` and open:
	```
	srt://<NLB_HOSTNAME>:5000?mode=caller
	```

---

## 6. Cleanup

To stop AWS charges, delete the cluster and images:

```bash
# Delete K8s resources
kubectl delete -f examples/aws/kube-aws-2-nodes-efa.yaml

# Destroy Infrastructure
cd /path/to/mxl-dmf-terraform && terraform destroy

# Delete Repositories
aws ecr delete-repository --repository-name mxl-writer-efa --force
aws ecr delete-repository --repository-name mxl-reader-efa --force
```
