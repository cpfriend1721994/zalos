#!/usr/bin/env bash
set -eo pipefail

DOCKER="${DOCKER:-docker}"
BASE_IMAGE="$1"
PLATFORM="$2"
BUILD_SCRIPT="$3"
DIST_DIR="${4:-dist}"

if [ -z "${BASE_IMAGE}" ] || [ -z "${PLATFORM}" ] || [ -z "${BUILD_SCRIPT}" ]; then
    echo "❌ Usage: $0 <base-image> <platform> <packaging-script> [host-dist-dir]" >&2
    exit 2
fi

SCRIPT_TAG=$(echo "${BUILD_SCRIPT}" | tr -cs 'a-zA-Z0-9' '-' | sed 's/^-//;s/-$//')
SLUG="zalos-package-${SCRIPT_TAG}-${PLATFORM//\//-}-$$"
IMAGE="${SLUG}"
CONTAINER="${SLUG}"
trap '"${DOCKER}" rm -f "${CONTAINER}" >/dev/null 2>&1 || true' EXIT

echo "=========================================================="
echo "📦 ${BUILD_SCRIPT} on ${BASE_IMAGE} (${PLATFORM})"
echo "=========================================================="

echo "==> Baking the source into the base image..."
"${DOCKER}" build --platform "${PLATFORM}" -f Dockerfile.app --build-arg BASE_IMAGE="${BASE_IMAGE}" -t "${IMAGE}" .

case "${PLATFORM##*/}" in
    amd64) WANT_ARCH=x86_64 ;;
    arm64) WANT_ARCH=aarch64 ;;
    *) WANT_ARCH="${PLATFORM##*/}" ;;
esac
EMULATED=0
if [ "${WANT_ARCH}" != "$(uname -m)" ]; then
    EMULATED=1
fi
echo "==> Target arch ${WANT_ARCH} on host $(uname -m): emulated=${EMULATED}"

echo "==> Running ${BUILD_SCRIPT}..."
"${DOCKER}" run --name "${CONTAINER}" --platform "${PLATFORM}" -u 0:0 -e HOME=/tmp -e ZALOS_EMULATED="${EMULATED}" "${IMAGE}" \
    bash -eo pipefail -c "cd /workspace && ${BUILD_SCRIPT}"

echo "==> Copying /workspace/dist out of the container..."
mkdir -p "${DIST_DIR}"
"${DOCKER}" cp "${CONTAINER}:/workspace/dist/." "${DIST_DIR}/"
"${DOCKER}" cp "${CONTAINER}:/workspace/flake.lock" ./flake.lock >/dev/null 2>&1 || true
ls -lh "${DIST_DIR}"

"${DOCKER}" rmi -f "${IMAGE}" >/dev/null 2>&1 || true
echo "✅ ${BUILD_SCRIPT} (${PLATFORM}) done"
