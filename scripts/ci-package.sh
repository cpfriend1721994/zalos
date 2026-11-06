#!/usr/bin/env bash
set -eo pipefail

#==========================================================
# Build one release package without any host bind mount.
#
#   ci-package.sh <base-image> <platform> <packaging-script> [<host-dist-dir>]
#   ci-package.sh zalos-fedora:43-amd64 linux/amd64 packaging/rpm/build-rpm.sh
#   DOCKER=podman ci-package.sh localhost/zalos-ubuntu:24.04 linux/amd64 packaging/deb/build-deb.sh
#
# The docker-compose services (local development) mount the checkout into /workspace, which needs the engine to see
# the job's filesystem. A CI runner with a remote or containerized daemon gets an empty /workspace instead, so the
# packaging script fails with exit 127. Here the source and the artifacts move through the docker API:
#   1. docker build  — Dockerfile.app COPYs the checkout on top of a prebuilt toolchain base (no RUN, so the
#                      linux/arm64 build needs no emulation),
#   2. docker run    — the packaging script runs inside that image and writes /workspace/dist in the container,
#   3. docker cp     — dist/ is copied out to the host workspace for upload-artifact.
# Called by the packaging jobs in .github/workflows/ci.yml.
#==========================================================

DOCKER="${DOCKER:-docker}"
BASE_IMAGE="$1"
PLATFORM="$2"
BUILD_SCRIPT="$3"
DIST_DIR="${4:-dist}"

if [ -z "${BASE_IMAGE}" ] || [ -z "${PLATFORM}" ] || [ -z "${BUILD_SCRIPT}" ]; then
    echo "❌ Usage: $0 <base-image> <platform> <packaging-script> [host-dist-dir]" >&2
    exit 2
fi

# Per-run names: several targets share one daemon and runner, and an old container must never be reused.
SLUG="zalos-package-$(basename "${BUILD_SCRIPT}" .sh)-"${PLATFORM//\//-}-$$
IMAGE="${SLUG}"
CONTAINER="${SLUG}"
trap '"${DOCKER}" rm -f "${CONTAINER}" >/dev/null 2>&1 || true' EXIT

echo "=========================================================="
echo "📦 ${BUILD_SCRIPT} on ${BASE_IMAGE} (${PLATFORM})"
echo "=========================================================="

echo "==> Baking the source into the base image..."
"${DOCKER}" build --platform "${PLATFORM}" -f Dockerfile.app --build-arg BASE_IMAGE="${BASE_IMAGE}" -t "${IMAGE}" .

# Run as root inside the (throwaway) image: the COPYed tree is root-owned, and rootless podman would otherwise
# start the container as the host user and fail to write build trees into /workspace. -u 0:0 is root in rootful
# docker and maps to the invoking user in rootless podman, so the copied-out artifacts stay readable.
# HOME=/tmp: nothing may be written into /workspace by the toolchain itself (the local compose services share
# HOME=/workspace and leave caches behind; keep the same rule here).
echo "==> Running ${BUILD_SCRIPT}..."
"${DOCKER}" run --name "${CONTAINER}" --platform "${PLATFORM}" -u 0:0 -e HOME=/tmp "${IMAGE}" \
    bash "/workspace/${BUILD_SCRIPT}"

echo "==> Copying /workspace/dist out of the container..."
mkdir -p "${DIST_DIR}"
"${DOCKER}" cp "${CONTAINER}:/workspace/dist/." "${DIST_DIR}/"
ls -lh "${DIST_DIR}"

"${DOCKER}" rmi -f "${IMAGE}" >/dev/null 2>&1 || true
echo "✅ ${BUILD_SCRIPT} (${PLATFORM}) done"
