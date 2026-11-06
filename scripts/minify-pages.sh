#!/usr/bin/env bash
set -eo pipefail

#==========================================================
# Build the minified GitHub Pages site: docs/ -> _site/ (no host bind mounts).
#
#   bash scripts/minify-pages.sh [output-dir]        # default: <repo>/_site
#   DOCKER=podman bash scripts/minify-pages.sh
#
# The readable source of truth stays docs/index.html; this only produces the deployable
# artifact. The site is baked inside Dockerfile.pages (COPY docs) and copied out through
# the docker API (`docker create` + `docker cp`), so the same script works on a local
# machine and on the CI runner whose daemon cannot see the checkout.
# Called by .github/workflows/pages.yml before upload-pages-artifact.
#==========================================================

DOCKER="${DOCKER:-docker}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${1:-${ROOT_DIR}/_site}"

IMAGE="zalos-pages-minify-$$"
CONTAINER="${IMAGE}"
CONTEXT_DIR="$(mktemp -d)"
trap '{ "${DOCKER}" rm -f "${CONTAINER}" >/dev/null 2>&1 || true; rm -rf "${CONTEXT_DIR}"; }' EXIT

echo "=========================================================="
echo "🌐 Minifying GitHub Pages site (docs/ -> ${OUT_DIR#${ROOT_DIR}/})"
echo "=========================================================="

echo "==> Staging a minimal build context (docs + packaging/pages only)..."
mkdir -p "${CONTEXT_DIR}/packaging"
cp -r "${ROOT_DIR}/docs" "${CONTEXT_DIR}/docs"
cp -r "${ROOT_DIR}/packaging/pages" "${CONTEXT_DIR}/packaging/pages"

echo "==> Baking the minified site into the image..."
"${DOCKER}" build -f "${CONTEXT_DIR}/packaging/pages/Dockerfile.pages" -t "${IMAGE}" "${CONTEXT_DIR}"

echo "==> Copying the artifact out (docker cp, no bind mount)..."
CONTAINER_ID=$("${DOCKER}" create "${IMAGE}")
rm -rf "${OUT_DIR}"
mkdir -p "$(dirname "${OUT_DIR}")"
"${DOCKER}" cp "${CONTAINER_ID}:/app/out" "${OUT_DIR}"
"${DOCKER}" rm -f "${CONTAINER_ID}" >/dev/null
"${DOCKER}" rmi -f "${IMAGE}" >/dev/null 2>&1 || true

echo "==> Result:"
SRC_SIZE=$(du -h "${ROOT_DIR}/docs/index.html" | cut -f1)
MIN_SIZE=$(du -h "${OUT_DIR}/index.html" | cut -f1)
echo "    index.html : ${SRC_SIZE} -> ${MIN_SIZE} (minified)"
echo "    assets     : $(du -sh "${OUT_DIR}/assets" | cut -f1)"
echo "✅ Minified Pages site ready at ${OUT_DIR}"
