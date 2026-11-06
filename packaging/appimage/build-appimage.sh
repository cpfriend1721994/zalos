#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos AppImage"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
# Target architecture = the container's architecture (x86_64 natively, aarch64 under an arm64/QEMU container).
ARCH="$(uname -m)"
# Own build tree: a Release, tests-off configure must not replace the test build in build/.
BUILD_DIR="${WORKSPACE_DIR}/build-appimage"
APP_DIR="${BUILD_DIR}/AppDir"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"

mkdir -p "${BUILD_DIR}" "${APP_DIR}" "${OUTPUT_DIR}"

# 1. Compile with CMake (or reuse build-deb if already built in the same Ubuntu 24.04 environment)
DISTRO_STAMP="$(. /etc/os-release && echo "${ID}-${VERSION_ID}")"
if [ -x "${WORKSPACE_DIR}/build-deb/zalos" ] && [ -f "${WORKSPACE_DIR}/build-deb/CMakeCache.txt" ] \
   && [ ! "${WORKSPACE_DIR}/CMakeLists.txt" -nt "${WORKSPACE_DIR}/build-deb/CMakeCache.txt" ]; then
    echo "==> Reusing Release build from build-deb..."
    BUILD_DIR="${WORKSPACE_DIR}/build-deb"
    cmake --build "${BUILD_DIR}" --config Release -j"$(nproc)"
else
    echo "==> Configuring and compiling Zalos..."
    # The tree may come from another base image (e.g. a Fedora build before the AppImage moved to Ubuntu): a CMake
    # cache from a different distro points at missing Qt/OpenSSL paths, so start over when the distro changed.
    if [ -f "${BUILD_DIR}/CMakeCache.txt" ] && [ "$(cat "${BUILD_DIR}/.distro" 2>/dev/null)" != "${DISTRO_STAMP}" ]; then
        echo "==> Build tree from another distro: reconfiguring from scratch"
        rm -rf "${BUILD_DIR:?}"
    fi
    mkdir -p "${BUILD_DIR}"
    echo "${DISTRO_STAMP}" > "${BUILD_DIR}/.distro"
    cmake -B "${BUILD_DIR}" -S "${WORKSPACE_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF \
        -DZALOS_QML_AOT=OFF # runs on every host Qt >= 6.4 (see CMakeLists.txt)

    cmake --build "${BUILD_DIR}" --config Release -j"$(nproc)"
fi
rm -rf "${APP_DIR}"
DESTDIR="${APP_DIR}" cmake --install "${BUILD_DIR}" --strip

# Qt and the desktop stack come from the host (Qt >= 6.4, glibc >= 2.39: built on Ubuntu 24.04). The one library
# that is not universal is OpenSSL's libcrypto.so.3 (Fedora 45+ ships OpenSSL 4 by default), so it is bundled.
# Never bundle libopenh264 (dlopen()ed from the host) or libGL/libEGL/libvulkan.
# AppRun only uses it when the host has no libcrypto.so.3 (a host copy is newer and Qt's TLS plugin needs it).
mkdir -p "${APP_DIR}/usr/lib/openssl3-fallback"
CRYPTO_LIB=$(ldd "${APP_DIR}/usr/bin/zalos" | awk '/libcrypto\.so\.3/ {print $3}')
if [ -n "${CRYPTO_LIB}" ]; then
    cp -L "${CRYPTO_LIB}" "${APP_DIR}/usr/lib/openssl3-fallback/"
    echo "==> Bundled $(basename "${CRYPTO_LIB}")"
fi

# Animated-sticker WebP image plugin (libqwebp) + its libwebp chain, as a fallback for a host whose Qt has no WebP
# support (e.g. no qt6-image-formats-plugins). Kept out of the global plugin path: AppRun adds it only then, so a
# host plugin (matching the host Qt) always wins. The plugin's RUNPATH points at the bundled libwebp beside it, so
# it never pulls a mismatched system libwebp.
echo "==> Bundling WebP image plugin (animated stickers fallback)..."
WEBP_DIR="${APP_DIR}/usr/lib/webp-fallback"
QT_PLUGIN_SRC=$(find /usr/lib -type d -name imageformats -path "*qt6*" 2>/dev/null | head -1)
WEBP_PLUGIN="${QT_PLUGIN_SRC}/libqwebp.so"
if [ -f "${WEBP_PLUGIN}" ]; then
    mkdir -p "${WEBP_DIR}/imageformats"
    cp -L "${WEBP_PLUGIN}" "${WEBP_DIR}/imageformats/"
    # Copy the libwebp chain the plugin needs (not Qt/system-universal libraries).
    ldd "${WEBP_PLUGIN}" | awk '/libwebp|libsharpyuv/ {print $3}' | while read -r lib; do
        [ -n "${lib}" ] && cp -L "${lib}" "${WEBP_DIR}/"
    done
    # The plugin loads its libwebp from beside it, never a mismatched system copy.
    patchelf --set-rpath '$ORIGIN/..' "${WEBP_DIR}/imageformats/libqwebp.so"
    echo "==> Bundled libqwebp.so + $(ls "${WEBP_DIR}"/*.so* 2>/dev/null | wc -l) libwebp libraries"
else
    echo "==> libqwebp.so not found in the build image; skipping (host Qt must provide WebP)"
fi

# 2. Setup AppDir skeleton
echo "==> Preparing AppDir metadata..."
cp "${WORKSPACE_DIR}/packaging/appimage/AppRun" "${APP_DIR}/AppRun"
chmod +x "${APP_DIR}/AppRun"

cp "${WORKSPACE_DIR}/assets/zalos.desktop" "${APP_DIR}/zalos.desktop"
cp "${WORKSPACE_DIR}/assets/icons/zalos.svg" "${APP_DIR}/zalos.svg"

# Convert SVG to PNG for AppImage icon specification
if command -v rsvg-convert &>/dev/null; then
    rsvg-convert -w 256 -h 256 "${WORKSPACE_DIR}/assets/icons/zalos.svg" -o "${APP_DIR}/zalos.png"
    cp "${APP_DIR}/zalos.png" "${APP_DIR}/.DirIcon"
fi

# 3. Download appimagetool if not present (architecture-matched: x86_64 or aarch64)
mkdir -p /tmp/tools
TOOL="/tmp/tools/appimagetool-${ARCH}"
if [ ! -f "${TOOL}" ]; then
    echo "==> Downloading appimagetool (${ARCH})..."
    curl -sSL -o "${TOOL}" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" || true
    chmod +x "${TOOL}" || true
fi

# 4. Generate AppImage or fallback archive
rm -f "${OUTPUT_DIR}/Zalos-1.0.4-${ARCH}.AppImage"
if [ -f "${TOOL}" ] && [ -x "${TOOL}" ]; then
    echo "==> Generating AppImage via appimagetool..."
    ARCH="${ARCH}" "${TOOL}" --appimage-extract-and-run "${APP_DIR}" "${OUTPUT_DIR}/Zalos-1.0.4-${ARCH}.AppImage" || {
        echo "Fallback to portable archive..."
        tar -czf "${OUTPUT_DIR}/Zalos-1.0.4-${ARCH}-AppDir.tar.gz" -C "${APP_DIR}" .
    }
else
    echo "==> Packaging portable AppDir bundle..."
    tar -czf "${OUTPUT_DIR}/Zalos-1.0.4-${ARCH}-AppDir.tar.gz" -C "${APP_DIR}" .
fi

echo "✅ AppImage build step completed. Artifacts saved in dist/"

