#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos .deb (Ubuntu 24.04 / Qt 6.4 baseline)"
echo "=========================================================="

# Built on the oldest supported release (Ubuntu 24.04, Qt 6.4.2, glibc 2.39) so the package installs on 24.04
# and every newer Ubuntu / Debian 13+: Qt keeps binary compatibility within Qt 6, newer distros only add symbols.
WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
BUILD_DIR="${WORKSPACE_DIR}/build-deb"
PKG_DIR="${BUILD_DIR}/pkg"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"
ZALOS_VERSION="1.0.7"
REVISION="1"
ARCH="$(dpkg --print-architecture)"
. /etc/os-release
DEB_NAME="zalos_${ZALOS_VERSION}-${REVISION}~ubuntu${VERSION_ID}_${ARCH}.deb"

mkdir -p "${OUTPUT_DIR}"

echo "==> Configuring and compiling Zalos..."
cmake -B "${BUILD_DIR}" -S "${WORKSPACE_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DZALOS_QML_AOT=OFF # runs on every newer Qt (see CMakeLists.txt)
BUILD_JOBS="$(nproc)"
if [ "${ZALOS_EMULATED:-0}" = "1" ] && [ "${BUILD_JOBS}" -gt 2 ]; then
    BUILD_JOBS=2
fi
cmake --build "${BUILD_DIR}" -j"${BUILD_JOBS}"

rm -rf "${PKG_DIR}"
DESTDIR="${PKG_DIR}" cmake --install "${BUILD_DIR}" --strip

echo "==> Resolving shared library dependencies..."
# dpkg-shlibdeps needs a debian/control next to it; use a throwaway one.
SHLIBS_TMP="$(mktemp -d)"
mkdir -p "${SHLIBS_TMP}/debian"
printf 'Source: zalos\n\nPackage: zalos\nArchitecture: any\n' > "${SHLIBS_TMP}/debian/control"
SHLIB_DEPENDS=$(cd "${SHLIBS_TMP}" && dpkg-shlibdeps -O "${PKG_DIR}/usr/bin/zalos" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
rm -rf "${SHLIBS_TMP}"
# Debian's Qt shlibs add an exact private-ABI pin (e.g. qt6-websockets-abi (= 6.4.2)) to every consumer; zalos uses
# only public Qt API, which stays binary compatible across Qt 6, and the pin would block newer Ubuntu releases.
SHLIB_DEPENDS=$(echo "${SHLIB_DEPENDS}" | sed -E 's/(, )?qt6-[a-z0-9-]+-abi \(= [^)]*\)//g; s/^, //')
# Ubuntu 24.04 renamed libraries with a t64 suffix (64-bit time_t transition); later releases drop it again for some
# (26.10: libqt6dbus6, libqt6gui6, ... but still libqt6core6t64). Accept either name.
SHLIB_DEPENDS=$(echo "${SHLIB_DEPENDS}" | sed -E 's/([a-z0-9.+-]+)t64( \([^)]*\))?/\1t64\2 | \1\2/g')

# QML modules are loaded at run time (no ELF link), so they are listed by hand: keep in sync with the QML imports.
QML_DEPENDS="qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, \
qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtquick-shapes, qml6-module-qtquick-dialogs, \
qml6-module-qtqml-workerscript, qml6-module-qtmultimedia, qml6-module-qtcore, qt6-wayland, \
libqt6svg6 (>= 6.4), qt6-image-formats-plugins" # libqt6svg6: SVG plugins; image-formats: WebP (animated stickers)

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
Depends: ${SHLIB_DEPENDS}, ${QML_DEPENDS}, python3, nodejs, libopus0
Recommends: libopenh264-7 | libopenh264-8, pulseaudio-utils | alsa-utils
Description: Native Qt6 (C++/QML) desktop client for Zalo on Linux
 Zalos is a native Qt 6 client for the Zalo messenger: QR sign-in, messaging,
 groups, media and calls. The zcall-native call engine (zalo-for-linux, MIT)
 ships in /usr/share/zalos/zcall-native and runs on the system's node and
 python3; call video uses the system's OpenH264, which is never bundled.
CONTROL

echo "==> Building ${DEB_NAME}..."
dpkg-deb --build --root-owner-group "${PKG_DIR}" "${OUTPUT_DIR}/${DEB_NAME}"
dpkg-deb --info "${OUTPUT_DIR}/${DEB_NAME}"
echo "✅ .deb saved: dist/${DEB_NAME}"
