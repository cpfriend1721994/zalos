#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos for Arch Linux (makepkg, from source)"
echo "=========================================================="

# Runs as root in a clean archlinux:latest container: installs the build deps, then makepkg as an unprivileged
# user (makepkg refuses root) with a source tarball made from the checkout (only git-visible files, like the RPM).
WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"
PKGVER=$(sed -n 's/^pkgver=//p' "${WORKSPACE_DIR}/packaging/arch/PKGBUILD")
WORK=/tmp/arch-build

pacman -Syu --noconfirm --needed base-devel git cmake ninja qt6-base qt6-declarative qt6-svg qt6-websockets \
    qt6-multimedia qt6-wayland qt6-shadertools openssl xz zlib sqlite python hicolor-icon-theme openh264 >/dev/null

rm -rf "${WORK}" && mkdir -p "${WORK}"
cp "${WORKSPACE_DIR}/packaging/arch/PKGBUILD" "${WORK}/"
( cd "${WORKSPACE_DIR}" &&
  git -c safe.directory='*' ls-files -z --cached --others --exclude-standard |
  while IFS= read -r -d '' f; do [ -f "$f" ] && printf '%s\0' "$f"; done ) > /tmp/zalos-src-files
if [ ! -s /tmp/zalos-src-files ]; then
    echo "❌ git ls-files returned nothing: refusing to tar the whole workspace" >&2
    exit 1
fi
# Same name and top directory as GitHub's tag archive, so makepkg uses it instead of downloading.
tar -C "${WORKSPACE_DIR}" --transform "s,^,zalos-${PKGVER}/," \
    -czf "${WORK}/zalos-${PKGVER}.tar.gz" --null -T /tmp/zalos-src-files

id builder >/dev/null 2>&1 || useradd -m builder
chown -R builder: "${WORK}"
su builder -c "cd ${WORK} && makepkg --noconfirm --nodeps"

mkdir -p "${OUTPUT_DIR}"
cp -v "${WORK}"/zalos-"${PKGVER}"-*-x86_64.pkg.tar.zst "${OUTPUT_DIR}/"
echo "✅ Arch package saved in dist/"
