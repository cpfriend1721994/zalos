#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos .deb (Qt 6.8.3 LTS + QML AOT)"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
BUILD_DIR="${WORKSPACE_DIR}/build-deb"
PKG_DIR="${BUILD_DIR}/pkg"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"
ZALOS_VERSION="1.3.1"
REVISION="1"
ARCH="$(dpkg --print-architecture)"
. /etc/os-release
DEB_NAME="zalos_${ZALOS_VERSION}-${REVISION}~ubuntu${VERSION_ID}_${ARCH}.deb"
QT_ROOT="${QT_ROOT:-/opt/qt6}"

mkdir -p "${OUTPUT_DIR}"

echo "==> Configuring and compiling Zalos (Qt 6.8.3 + QML AOT)..."
cmake -B "${BUILD_DIR}" -S "${WORKSPACE_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DZALOS_QML_AOT=ON
BUILD_JOBS="$(nproc)"
if [ "${ZALOS_EMULATED:-0}" = "1" ] && [ "${BUILD_JOBS}" -gt 5 ]; then
    BUILD_JOBS=5
fi
cmake --build "${BUILD_DIR}" -j"${BUILD_JOBS}"

rm -rf "${PKG_DIR}"
DESTDIR="${PKG_DIR}" cmake --install "${BUILD_DIR}" --strip

echo "==> Bundling surgical Qt 6.8.3 LTS runtime..."
bash "${WORKSPACE_DIR}/packaging/common/bundle-qt6.sh" \
    "${QT_ROOT}" \
    "${PKG_DIR}/usr/lib/zalos/qt6" \
    "${PKG_DIR}/usr/bin" \
    "${PKG_DIR}/usr/libexec/zalos" \
    --build-icu-shim

echo "==> Bundling fallback libopus.so.0..."
FALLBACK_DIR="${PKG_DIR}/usr/lib/zalos/fallback"
mkdir -p "${FALLBACK_DIR}"
OPUS_LIB="$(ldconfig -p | sed -n 's/^[[:space:]]*libopus\.so\.0 (.*) => //p' | head -n1)"
if [ -z "${OPUS_LIB}" ] || [ ! -e "${OPUS_LIB}" ]; then
    echo "❌ libopus.so.0 not found in the build image" >&2
    exit 1
fi
install -m 0644 "$(readlink -f "${OPUS_LIB}")" "${FALLBACK_DIR}/libopus.so.0"

PKG_DEPENDS="libc6 (>= 2.34), libstdc++6 (>= 12), libgl1, libegl1, libfontconfig1, libfreetype6, \
libdbus-1-3, libglib2.0-0, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-shape0, libxkbcommon0, libxkbcommon-x11-0, \
libwayland-client0, libwayland-cursor0, libwayland-egl1, libxrandr2, libpulse0, libgssapi-krb5-2, libssl3t64 | libssl3, libsqlite3-0, liblzma5, \
zlib1g, libzstd1, libpng16-16t64 | libpng16-16, \
libicu76 | libicu74 | libicu72 | libicu71 | libicu70 | libicu78 | libicu77 | libicu75"

INSTALLED_SIZE=$(du -sk "${PKG_DIR}" | cut -f1)
mkdir -p "${PKG_DIR}/DEBIAN"
cat > "${PKG_DIR}/DEBIAN/control" <<CONTROL
Package: zalos
Version: ${ZALOS_VERSION}-${REVISION}~ubuntu${VERSION_ID}
Architecture: ${ARCH}
Maintainer: Zalos Developers <noreply@zalos.invalid>
Installed-Size: ${INSTALLED_SIZE}
Section: net
Priority: optional
Homepage: https://github.com/cpfriend1721994/zalos
Depends: ${PKG_DEPENDS}
Recommends: libopus0, libopenh264-7 | libopenh264-8
Suggests: cups-client
Description: Native Qt6 (C++/QML AOT) messaging client for Zalo on Linux
 Zalos is an open-source Qt 6 desktop client for Zalo with bundled Qt 6.8 LTS
 and QML AOT compilation: zero system Qt dependency across distributions.
 Calls run on the bundled C++ call engine (zalos-call and zalos-call-audio in
 /usr/libexec/zalos). OpenH264 video codec is never bundled.
CONTROL

echo "==> Building ${DEB_NAME}..."
dpkg-deb -Zxz -z9 --build --root-owner-group "${PKG_DIR}" "${OUTPUT_DIR}/${DEB_NAME}"
dpkg-deb --info "${OUTPUT_DIR}/${DEB_NAME}"
echo "✅ .deb saved: dist/${DEB_NAME}"
