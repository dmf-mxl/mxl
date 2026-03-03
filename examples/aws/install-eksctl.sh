set -e
if command -v eksctl >/dev/null 2>&1; then
  echo "eksctl already installed: $(eksctl version)"
  exit 0
fi
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64) ARCH="amd64" ;;
  aarch64|arm64) ARCH="arm64" ;;
  *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac
PLATFORM="$(uname -s)_${ARCH}"
TMP_DIR="$(mktemp -d)"
cd "$TMP_DIR"
curl -sSL -o eksctl.tar.gz "https://github.com/eksctl-io/eksctl/releases/latest/download/eksctl_${PLATFORM}.tar.gz"
tar -xzf eksctl.tar.gz
mkdir -p "$HOME/.local/bin"
install -m 0755 eksctl "$HOME/.local/bin/eksctl"
rm -rf "$TMP_DIR"
if ! echo "$PATH" | tr ':' '\n' | grep -qx "$HOME/.local/bin"; then
  echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
  export PATH="$HOME/.local/bin:$PATH"
fi
eksctl version