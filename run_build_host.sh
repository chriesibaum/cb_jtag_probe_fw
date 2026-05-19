#!/usr/bin/env bash
# Run the Zephyr build-host container with Podman.
#
# Usage:
#   ./run.sh [workspace-path]        # default: current directory
#
# Environment overrides:
#   IMAGE               full image reference  (default: zephyr-build-host:v4.4.0_sdk-1.0.1)
#   CCACHE_DIR          host ccache directory (default: ~/.cache/ccache)
#   PYOCD_PACKS_DIR     mount a host directory over /pyocd-packs to replace the
#                       baked-in packs; leave unset to use the image's packs
#   FLASH               pass USB bus into container for flashing/debugging (default: 1)
#   WORK_DIR            initial working directory inside the container, relative to the
#                       workspace mount; omit to start in /workspace  (default: unset)
#
# Examples:
#   ./run.sh ~/projects/my-zephyr-workspace
#   FLASH=1 ./run.sh ~/projects/my-zephyr-workspace
#   IMAGE=zephyr-build-host:v4.4.0_sdk-1.0.1 ./run.sh
#   WORK_DIR=./cb_black_box.fw/app ./run.sh ~/projects/my-zephyr-workspace

set -euo pipefail

IMAGE="${IMAGE:-zephyr-build-host:v4.4.0_sdk-1.0.1}"
WORKSPACE="$(realpath "${1:-${PWD}}")"
CCACHE_DIR="${CCACHE_DIR:-${HOME}/.cache/ccache}"

# Resolve WORK_DIR to an absolute path inside /workspace
if [[ -n "${WORK_DIR:-}" ]]; then
    # Strip a leading ./ or / so we can safely join with /workspace
    WORK_DIR_REL="${WORK_DIR#./}"
    WORK_DIR_REL="${WORK_DIR_REL#/}"
    CONTAINER_WORKDIR="/workspace/${WORK_DIR_REL%/}"
else
    CONTAINER_WORKDIR="/workspace"
fi
DOCKER_HOST_HOSTNAME="$(hostname --fqdn)"

RUN_ARGS=(
    --rm
    --interactive
    --tty
    --hostname "${DOCKER_HOST_HOSTNAME}/${IMAGE}"
    # Keep the host user's UID/GID inside the container so bind-mounted
    # directories remain writable (rootless Podman user-namespace fix).
    --userns=keep-id
    # Bind-mount the Zephyr west workspace into the container.
    # The :z label relabels the mount for SELinux/seccomp on Fedora-based hosts.
    --volume "${WORKSPACE}:/workspace:z"
)

# ccache: speeds up repeated builds by caching compiler outputs.
# Mounted to /ccache (fixed path); CCACHE_DIR is set to that path in the image.
mkdir -p "${CCACHE_DIR}"
RUN_ARGS+=(--volume "${CCACHE_DIR}:/ccache:z")

# pyOCD CMSIS-Pack directory: optional host override for the baked-in packs.
# Set PYOCD_PACKS_DIR to a host directory to mount it over /pyocd-packs;
# this replaces the baked-in packs with the contents of that directory.
# Leave unset to use the packs that were installed into the image at build time.
if [[ -n "${PYOCD_PACKS_DIR:-}" ]]; then
    mkdir -p "${PYOCD_PACKS_DIR}"
    RUN_ARGS+=(--volume "${PYOCD_PACKS_DIR}:/pyocd-packs:z")
fi

# Git configuration – forward the host config read-only so 'west update' works
if [[ -f "${HOME}/.gitconfig" ]]; then
    USERNAME="$(podman image inspect --format '{{.Config.User}}' "${IMAGE}" 2>/dev/null || true)"
    USERNAME="${USERNAME:-calvin}"
    RUN_ARGS+=(--volume "${HOME}/.gitconfig:/home/${USERNAME}/.gitconfig:ro,z")
fi

# SSH agent – forward the host socket so private repos can be cloned
if [[ -n "${SSH_AUTH_SOCK:-}" && -S "${SSH_AUTH_SOCK}" ]]; then
    RUN_ARGS+=(
        --volume "${SSH_AUTH_SOCK}:/run/ssh-agent.sock:z"
        --env    "SSH_AUTH_SOCK=/run/ssh-agent.sock"
    )
fi

# USB passthrough for flashing / debugging (OpenOCD, pyOCD, J-Link)
# Requires the host user to have access to /dev/bus/usb (e.g. via udev rules)
if [[ "${FLASH:-1}" == "1" ]]; then
    echo "USB passthrough enabled."
    RUN_ARGS+=(
        --device=/dev/bus/usb
        --group-add keep-groups   # preserve host supplementary groups (e.g. plugdev) for USB device access
    )
    # Mount /dev/disk read-only so mbed-ls (used by pyOCD) can resolve
    # /dev/disk/by-id symlinks and suppresses the "Could not get disk devices" warning.
    [[ -d /dev/disk ]] && RUN_ARGS+=(--volume "/dev/disk:/dev/disk:ro")
fi

# X11 forwarding – allows GUI programs (Ozone, J-Link GDB Server GUI, …) to open windows
if [[ -n "${DISPLAY:-}" ]]; then
    RUN_ARGS+=(--env "DISPLAY=${DISPLAY}")
    [[ -d /tmp/.X11-unix ]] && RUN_ARGS+=(--volume "/tmp/.X11-unix:/tmp/.X11-unix:z")
    [[ -f "${HOME}/.Xauthority" ]] && RUN_ARGS+=(--volume "${HOME}/.Xauthority:/root/.Xauthority:ro,z")
fi

echo "──────────────────────────────────────────────"
echo " Image     : ${IMAGE}"
echo " Workspace : ${WORKSPACE}"
echo "            → /workspace  (inside container)"
echo "──────────────────────────────────────────────"
echo ""

# Show the patience note only the first time a given image is run with
# --userns=keep-id (Podman creates an ID-mapped layer copy on first use).
# The marker is keyed to the image ID, so a rebuilt image triggers it again.
IDMAP_MARKER_DIR="${XDG_CACHE_HOME:-${HOME}/.cache}/podman-idmap"
IMAGE_ID="$(podman image inspect --format '{{.Id}}' "${IMAGE}" 2>/dev/null | head -c 12)"
IDMAP_MARKER="${IDMAP_MARKER_DIR}/${IMAGE_ID}"

if [[ -n "${IMAGE_ID}" && ! -f "${IDMAP_MARKER}" ]]; then
    echo "NOTE: First run — Podman is creating an ID-mapped copy of the image"
    echo "      layers so bind-mount permissions work (--userns=keep-id)."
    echo "      For this large image that can take several minutes with no"
    echo "      visible output — please be patient and do NOT press Ctrl+C."
    echo ""
    mkdir -p "${IDMAP_MARKER_DIR}"
    touch "${IDMAP_MARKER}"
fi

exec podman run "${RUN_ARGS[@]}" --workdir "${CONTAINER_WORKDIR}" "${IMAGE}"
