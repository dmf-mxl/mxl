#! /bin/bash
# SPDX-FileCopyrightText: 2026 Contributors to the Media eXchange Layer project.
# SPDX-License-Identifier: Apache-2.0
#
# Build the MXL example images from source. Pre-built binaries and container
# images are not published.
#
# Can be run from a checkout, or without git:
#   curl -fsSL https://raw.githubusercontent.com/dmf-mxl/mxl/main/examples/bootstrap.sh | bash -s -- --yes --up
# Piped env vars must be on bash, not curl:
#   curl -fsSL ... | MXL_REF=<ref> bash -s -- --yes
# That downloads a source tarball (default: GitHub main) into ./mxl.
# MXL_SOURCE_DIR / MXL_REF / MXL_GITHUB_REPO apply only when this script is not
# already inside an MXL examples/ directory.
#
# If Docker Engine or Compose are missing, this script can install them with
# sudo from distro packages when those exist:
#   Ubuntu, Debian -> apt docker.io and docker-compose-v2
#   Fedora         -> dnf moby-engine and docker-compose
#   Arch Linux     -> pacman docker and docker-compose
# RHEL, CentOS, Rocky, and AlmaLinux have no distro Docker; those get Docker CE
# from Docker's yum/dnf repository. Do not mix docker.io and Docker CE.
# Other distros: install Docker and Compose yourself, then re-run.

set -euo pipefail

# Set after resolve_examples_dir (local checkout or fetched tarball).
EXAMPLES_DIR=""

# True if the caller set MXL_GITHUB_REPO or MXL_REF (before defaults).
MXL_SOURCE_EXPLICIT=0
if [[ -n "${MXL_GITHUB_REPO:-}" || -n "${MXL_REF:-}" ]]; then
    MXL_SOURCE_EXPLICIT=1
fi
MXL_GITHUB_REPO="${MXL_GITHUB_REPO:-dmf-mxl/mxl}"
MXL_REF="${MXL_REF:-main}"
MXL_SOURCE_STAMP=".mxl-bootstrap-source"

SUPPORTED_INSTALL='Ubuntu, Debian, Fedora, RHEL, CentOS, Rocky, AlmaLinux, Arch Linux'

YES=0
SKIP_DOCKER=0
SKIP_BUILD=0
RUN_UP=0

usage() {
    cat <<EOF
Usage: bootstrap.sh [options]

Build the in-tree MXL example images. From a clone:

  ./examples/bootstrap.sh

Or without git (downloads a source tarball into ./mxl):

  curl -fsSL https://raw.githubusercontent.com/${MXL_GITHUB_REPO}/main/examples/bootstrap.sh | bash -s -- --yes --up

  Put MXL_* on the bash process (curl ... | MXL_REF=<ref> bash -s -- --yes).
  Those variables are ignored when this script is already in an MXL examples/
  directory.

Environment:
  MXL_SOURCE_DIR    Where to unpack a tarball if this is not a checkout (default: ./mxl)
  MXL_REF           GitHub branch, tag, or commit SHA (7-40 hex, default: main)
  MXL_GITHUB_REPO   owner/name (default: dmf-mxl/mxl)

Options:
  --yes            Skip confirmation prompts (sudo may still ask for a password)
  --skip-docker    Do not install Docker/Compose; fail if they are missing
  --skip-build     Stop after Docker is available; do not build images
  --up             After a successful build, run 'docker compose up'
  -h, --help       Show this help

Requirements: Linux (x86_64 example image), network access, and sudo for a
first-time Docker install. Can install Docker on: ${SUPPORTED_INSTALL}.
EOF
}

log() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
    --yes) YES=1 ;;
    --skip-docker) SKIP_DOCKER=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    --up) RUN_UP=1 ;;
    -h | --help)
        usage
        exit 0
        ;;
    -*)
        die "unknown option: $1"
        ;;
    *)
        die "unexpected argument: $1"
        ;;
    esac
    shift
done

[[ "$(uname -s)" == "Linux" ]] || die "this script supports Linux hosts only"

OS_ID=""
OS_ID_LIKE=""
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    eval "$(. /etc/os-release && printf 'OS_ID=%q\nOS_ID_LIKE=%q\n' "${ID:-}" "${ID_LIKE:-}")"
fi

is_arch() {
    [[ "${OS_ID}" == "arch" || "${OS_ID}" == "archarm" || " ${OS_ID_LIKE} " == *" arch "* ]]
}

is_apt_family() {
    [[ "${OS_ID}" == "ubuntu" || "${OS_ID}" == "debian" || "${OS_ID}" == "raspbian" ]]
}

is_fedora() {
    [[ "${OS_ID}" == "fedora" || "${OS_ID}" == "fedora-asahi-remix" ]]
}

is_rhel_like() {
    [[ "${OS_ID}" == "rhel" || "${OS_ID}" == "centos" ||
        "${OS_ID}" == "rocky" || "${OS_ID}" == "almalinux" ]]
}

is_dnf_family() {
    is_fedora || is_rhel_like
}

can_install_docker() {
    is_arch || is_apt_family || is_dnf_family
}

need_sudo() {
    if [[ "$(id -u)" -eq 0 ]]; then
        return 0
    fi
    command -v sudo >/dev/null 2>&1 || die "sudo is required to install Docker"
    sudo -v || die "sudo authentication failed"
}

run_root() {
    if [[ "$(id -u)" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

pkg_install() {
    local pkgs=("$@")
    if is_arch; then
        run_root pacman -Sy --noconfirm --needed "${pkgs[@]}"
    elif is_apt_family; then
        run_root apt-get update -qq
        run_root apt-get install -y "${pkgs[@]}"
    elif is_dnf_family; then
        run_root dnf install -y "${pkgs[@]}"
    else
        return 1
    fi
}

ensure_download() {
    if command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1; then
        return 0
    fi
    log "Installing curl..."
    need_sudo
    pkg_install curl || die "install curl or wget, then re-run this script"
}

download() {
    local url="$1"
    local dest="$2"
    ensure_download
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$dest"
    else
        wget -qO "$dest" "$url"
    fi
}

in_examples_tree() {
    local dir="$1"
    [[ -f "${dir}/docker-compose.yaml" && -f "${dir}/Dockerfile" &&
        -f "${dir}/../lib/include/mxl/mxl.h" ]]
}

source_tarball_urls() {
    local ref="$1"
    if [[ "${ref}" =~ ^[0-9a-fA-F]{7,40}$ ]]; then
        printf '%s\n' "https://github.com/${MXL_GITHUB_REPO}/archive/${ref}.tar.gz"
    fi
    printf '%s\n' "https://github.com/${MXL_GITHUB_REPO}/archive/refs/heads/${ref}.tar.gz"
    printf '%s\n' "https://github.com/${MXL_GITHUB_REPO}/archive/refs/tags/${ref}.tar.gz"
}

requested_source_id() {
    printf '%s %s\n' "${MXL_GITHUB_REPO}" "${MXL_REF}"
}

write_source_stamp() {
    local dest="$1"
    requested_source_id >"${dest}/${MXL_SOURCE_STAMP}"
}

refuse_existing_tree() {
    local dest="$1"
    local why="$2"
    die "${why} Remove ${dest} or set MXL_SOURCE_DIR to a new directory."
}

use_existing_sources() {
    local dest="$1"
    log "Using existing sources at ${dest}"
    EXAMPLES_DIR="$(cd "${dest}/examples" && pwd)"
}

fetch_mxl_sources() {
    local dest="${MXL_SOURCE_DIR:-$PWD/mxl}"
    if in_examples_tree "${dest}/examples"; then
        local stamp="${dest}/${MXL_SOURCE_STAMP}"
        local want have=""
        want="$(requested_source_id)"
        if [[ -f "${stamp}" ]]; then
            have="$(tr -d '\r' <"${stamp}")"
            if [[ "${have}" != "${want}" ]]; then
                refuse_existing_tree "${dest}" \
                    "${dest} was fetched as ${have}, but this run asked for ${want}."
            fi
            use_existing_sources "${dest}"
            return 0
        fi
        if [[ "${MXL_SOURCE_EXPLICIT}" -eq 1 ]]; then
            refuse_existing_tree "${dest}" \
                "${dest} has no source stamp, so it cannot be verified as ${want}."
        fi
        use_existing_sources "${dest}"
        return 0
    fi
    if [[ -e "${dest}" ]] && [[ -n "$(ls -A "${dest}" 2>/dev/null || true)" ]]; then
        die "${dest} exists and is not an MXL source tree. Set MXL_SOURCE_DIR to a new or empty directory."
    fi

    command -v tar >/dev/null 2>&1 || {
        need_sudo
        pkg_install tar || die "install tar, then re-run this script"
    }

    mkdir -p "${dest}"
    local tmp url fetched=0
    tmp="$(mktemp)"
    log "Downloading MXL sources (${MXL_GITHUB_REPO} @ ${MXL_REF})..."
    while IFS= read -r url; do
        if download "${url}" "${tmp}" 2>/dev/null; then
            fetched=1
            break
        fi
    done < <(source_tarball_urls "${MXL_REF}")
    [[ "${fetched}" -eq 1 ]] || die "could not download a source archive for MXL_REF=${MXL_REF}"

    tar -xzf "${tmp}" -C "${dest}" --strip-components=1
    rm -f "${tmp}"
    in_examples_tree "${dest}/examples" || die "the archive did not contain examples/"
    write_source_stamp "${dest}"
    EXAMPLES_DIR="$(cd "${dest}/examples" && pwd)"
    log "Sources are in ${dest}"
}

resolve_examples_dir() {
    local src="${BASH_SOURCE[0]:-}"
    if [[ -n "${src}" && "${src}" != "bash" && -f "${src}" ]]; then
        local dir
        dir="$(cd "$(dirname "${src}")" && pwd)"
        if in_examples_tree "${dir}"; then
            EXAMPLES_DIR="${dir}"
            return 0
        fi
    fi
    fetch_mxl_sources
}

docker_ok() {
    command -v docker >/dev/null 2>&1 || return 1
    docker info >/dev/null 2>&1 && return 0
    sudo -n docker info >/dev/null 2>&1
}

compose_ok() {
    docker compose version >/dev/null 2>&1 && return 0
    sudo -n docker compose version >/dev/null 2>&1
}

docker_() {
    if docker info >/dev/null 2>&1; then
        command docker "$@"
    else
        sudo docker "$@"
    fi
}

start_docker_daemon() {
    docker_ok && return 0
    if command -v systemctl >/dev/null 2>&1 && [[ "$(ps -p 1 -o comm=)" == "systemd" ]]; then
        run_root systemctl enable --now docker
    elif command -v service >/dev/null 2>&1; then
        run_root service docker start
    fi
}

install_compose_plugin() {
    compose_ok && return 0

    log "Installing Docker Compose from distro packages..."
    if is_apt_family; then
        pkg_install docker-compose-v2 || pkg_install docker-compose-plugin || true
    elif is_fedora; then
        pkg_install docker-compose || pkg_install docker-compose-plugin || true
    elif is_rhel_like; then
        pkg_install docker-compose-plugin || true
    elif is_arch; then
        pkg_install docker-compose || true
    fi
    compose_ok || die "could not install Docker Compose v2 from distro packages. Install it yourself, then re-run."
}

finish_docker_install() {
    run_root groupadd -f docker
    if [[ "$(id -u)" -ne 0 ]]; then
        run_root usermod -aG docker "$(id -un)"
        log "Added $(id -un) to the docker group. Log out and back in (or restart the session) to use docker without sudo."
    fi

    start_docker_daemon || true
    docker_ok || die "Docker installed but the daemon is not reachable"
    install_compose_plugin
}

install_docker_arch() {
    log "Installing docker and docker-compose from Arch packages..."
    pkg_install docker docker-compose || die "pacman could not install docker and docker-compose"
}

install_docker_apt() {
    log "Installing docker.io and Docker Compose from distro packages..."
    pkg_install docker.io || die "apt could not install docker.io"
    pkg_install docker-compose-v2 || pkg_install docker-compose-plugin ||
        die "apt could not install Docker Compose v2 (docker-compose-v2)"
}

install_docker_fedora() {
    log "Installing moby-engine and Docker Compose from Fedora packages..."
    pkg_install moby-engine || die "dnf could not install moby-engine"
    pkg_install docker-compose || pkg_install docker-compose-plugin ||
        die "dnf could not install Docker Compose"
}

add_docker_ce_repo() {
    local url="$1"
    run_root dnf -y -q --setopt=install_weak_deps=False install dnf-plugins-core
    if command -v dnf5 >/dev/null 2>&1; then
        run_root dnf5 config-manager addrepo --overwrite --save-filename=docker-ce.repo --from-repofile="${url}"
    else
        run_root dnf config-manager --add-repo "${url}"
    fi
}

install_docker_ce() {
    local repo="rhel"
    [[ "${OS_ID}" == "centos" ]] && repo="centos"
    log "No distro Docker package on ${OS_ID}; installing Docker CE from Docker's ${repo} repository..."
    add_docker_ce_repo "https://download.docker.com/linux/${repo}/docker-ce.repo"
    run_root dnf install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin ||
        die "could not install Docker CE. Install Docker and Compose yourself, then re-run."
}

install_docker() {
    need_sudo

    if command -v docker >/dev/null 2>&1; then
        start_docker_daemon || true
        docker_ok || die "docker is installed but the daemon is not running"
        install_compose_plugin
        return 0
    fi

    if ! can_install_docker; then
        die "cannot install Docker on '${OS_ID:-unknown}'. Supported: ${SUPPORTED_INSTALL}. Install Docker and Compose, then re-run this script."
    fi

    if is_arch; then
        install_docker_arch
    elif is_apt_family; then
        install_docker_apt
    elif is_fedora; then
        install_docker_fedora
    elif is_rhel_like; then
        install_docker_ce
    else
        die "cannot install Docker on '${OS_ID:-unknown}'. Supported: ${SUPPORTED_INSTALL}."
    fi

    finish_docker_install
}

confirm_install() {
    [[ "${YES}" -eq 1 ]] && return 0
    local prompt answer
    if ! docker_ok; then
        prompt="Install Docker Engine and Compose on this machine? [y/N] "
    else
        prompt="Install the Docker Compose v2 plugin on this machine? [y/N] "
    fi
    printf '%s' "${prompt}"
    if [[ -t 0 ]]; then
        read -r answer
    elif [[ -r /dev/tty ]]; then
        read -r answer </dev/tty
    else
        die "refusing to install Docker without a tty; re-run with --yes"
    fi
    [[ "${answer}" == "y" || "${answer}" == "Y" ]] || die "aborted (install Docker yourself, or re-run with --yes)"
}

print_launch_help() {
    local maybe_sudo=""
    if ! docker info >/dev/null 2>&1; then
        maybe_sudo="sudo "
    fi
    cat <<EOF

Images are built. From ${EXAMPLES_DIR} :

  ${maybe_sudo}docker compose up
  ${maybe_sudo}docker compose up -d

Watch the video flow:
  ${maybe_sudo}docker logs -f mxl-example-video-flow-info-1

Watch the audio flow:
  ${maybe_sudo}docker logs -f mxl-example-audio-flow-info-1

Stop:
  ${maybe_sudo}docker compose down

See ${EXAMPLES_DIR}/README.md for Kubernetes export and binding the domain on the host.
EOF
}

resolve_examples_dir

if ! docker_ok || ! compose_ok; then
    [[ "${SKIP_DOCKER}" -eq 1 ]] && die "Docker or Compose is missing and --skip-docker was set"
    confirm_install
    install_docker
else
    log "Docker and Compose are already available."
fi

docker_ compose version >/dev/null || die "'docker compose' is not available"

if [[ "${SKIP_BUILD}" -eq 1 ]]; then
    log "Skipping image build (--skip-build). Examples are in ${EXAMPLES_DIR}"
    exit 0
fi

log "Building MXL example images from source (first run compiles the SDK and can take several minutes)..."
(
    cd "${EXAMPLES_DIR}"
    docker_ compose build
)

print_launch_help

if [[ "${RUN_UP}" -eq 1 ]]; then
    log "Starting the example compose stack..."
    cd "${EXAMPLES_DIR}"
    docker_ compose up
fi
