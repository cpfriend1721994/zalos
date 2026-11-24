#!/usr/bin/env bash
set -eo pipefail

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

echo "==> Verifying the cascade layers survived minification..."
SRC_LAYERS=$(grep -o '@layer ' "${ROOT_DIR}/docs/index.html" | wc -l || true)
OUT_LAYERS=$(grep -o '@layer ' "${OUT_DIR}/index.html" | wc -l || true)
SRC_MEDIA=$(grep -o '@media ' "${ROOT_DIR}/docs/index.html" | wc -l || true)
OUT_MEDIA=$(grep -o '@media ' "${OUT_DIR}/index.html" | wc -l || true)
if [ "${SRC_LAYERS}" -ne "${OUT_LAYERS}" ] || [ "${SRC_MEDIA}" -ne "${OUT_MEDIA}" ]; then
  echo "❌ Minifier dropped CSS (@layer ${SRC_LAYERS}->${OUT_LAYERS}, @media ${SRC_MEDIA}->${OUT_MEDIA}); refusing to deploy." >&2
  exit 1
fi

echo "==> Result:"
SRC_SIZE=$(du -h "${ROOT_DIR}/docs/index.html" | cut -f1)
MIN_SIZE=$(du -h "${OUT_DIR}/index.html" | cut -f1)
echo "    index.html : ${SRC_SIZE} -> ${MIN_SIZE} (minified)"
echo "    assets     : $(du -sh "${OUT_DIR}/assets" | cut -f1)"
echo "✅ Minified Pages site ready at ${OUT_DIR}"
