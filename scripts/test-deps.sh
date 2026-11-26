#!/usr/bin/env bash
set -eo pipefail

TARGET="${TARGET:?set TARGET=deb|rpm|appimage}"
. /etc/os-release
echo "=========================================================="
echo "🧪 Dependency check: ${TARGET} on ${PRETTY_NAME}"
echo "=========================================================="

DEBIAN_BASELINE="xvfb xauth libgl1 libegl1 libopengl0 libfontconfig1 libfreetype6 libdbus-1-3 libglib2.0-0 \
libx11-6 libx11-xcb1 libxcb1 libxkbcommon0 libxkbcommon-x11-0 libxrandr2 libwayland-client0 libwayland-cursor0 libwayland-egl1 \
libsm6 libice6 libpng16-16 libbrotli1 libzstd1 libgssapi-krb5-2 libudev1 libbz2-1.0 libpulse0 ca-certificates"
ARCH_BASELINE="xorg-server-xvfb xorg-xauth mesa libglvnd fontconfig freetype2 dbus glib2 libx11 libxcb libxkbcommon \
libxkbcommon-x11 libxrandr wayland libsm libice libpng brotli zstd krb5 systemd-libs bzip2 libpulse ca-certificates"
SUSE_BASELINE="xorg-x11-server-Xvfb xauth Mesa-libGL1 Mesa-libEGL1 libglvnd fontconfig libfreetype6 libdbus-1-3 \
libglib-2_0-0 libX11-6 libX11-xcb1 libxcb1 libxkbcommon0 libxrandr2 libwayland-client0 libwayland-cursor0 \
libwayland-egl1 libSM6 libICE6 libpng16-16 libbrotlidec1 libzstd1 krb5 libudev1 libbz2-1 libpulse0 ca-certificates"

ZALOS=""
case "${TARGET}" in
deb)
    DEB_FILE=$(ls -v /workspace/dist/zalos_*~ubuntu24.04_amd64.deb 2>/dev/null | tail -n 1)
    [ -n "${DEB_FILE}" ] || { echo "❌ No .deb in dist/ (podman compose run --rm package-deb)"; exit 1; }
    cp "${DEB_FILE}" /tmp/zalos.deb
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends /tmp/zalos.deb xvfb xauth >/dev/null
    dpkg -s zalos | grep -E '^(Version|Depends|Recommends)'
    ZALOS=/usr/bin/zalos
    ;;
rpm)
    RPM_FILE=$(ls -v /workspace/dist/zalos-*.x86_64.rpm 2>/dev/null | tail -n 1)
    [ -n "${RPM_FILE}" ] || { echo "❌ No RPM in dist/ (podman compose run --rm package-rpm)"; exit 1; }
    dnf install -y -q --setopt=fastestmirror=True --setopt=zchunk=False --setopt=install_weak_deps=False "${RPM_FILE}" xorg-x11-server-Xvfb xorg-x11-xauth >/dev/null \
        || { dnf clean all -q && dnf install -y -q --setopt=fastestmirror=True --setopt=zchunk=False --setopt=install_weak_deps=False "${RPM_FILE}" xorg-x11-server-Xvfb xorg-x11-xauth >/dev/null; }
    rpm -q --requires zalos | grep -v '^rpmlib' | tr '\n' ' '; echo
    ZALOS=/usr/bin/zalos
    ;;
appimage)
    APPIMAGE_FILE=$(ls -v /workspace/dist/Zalos-*-x86_64.AppImage 2>/dev/null | tail -n 1)
    [ -n "${APPIMAGE_FILE}" ] || { echo "❌ No AppImage in dist/ (podman compose run --rm package-appimage)"; exit 1; }
    case "${ID}" in
    ubuntu|debian)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq && apt-get install -y -qq --no-install-recommends ${DEBIAN_BASELINE} >/dev/null ;;
    arch)
        pacman -Syu --noconfirm --needed ${ARCH_BASELINE} >/dev/null ;;
    opensuse*)
        zypper -n -q install --no-recommends ${SUSE_BASELINE} >/dev/null ;;
    *) echo "❌ unsupported distro ${ID}"; exit 1 ;;
    esac
    rm -rf /tmp/appimage && mkdir -p /tmp/appimage && cd /tmp/appimage
    "${APPIMAGE_FILE}" --appimage-extract >/dev/null
    ZALOS=/tmp/appimage/squashfs-root/AppRun
    ;;
*) echo "❌ unknown TARGET ${TARGET}"; exit 1 ;;
esac

if [ "${TARGET}" != "appimage" ]; then
    MISSING=$(ldd "${ZALOS}" | grep 'not found' || true)
    [ -z "${MISSING}" ] || { echo "❌ missing libraries:"; echo "${MISSING}"; exit 1; }
fi

export HOME=/tmp/zalos-home
mkdir -p "${HOME}"
Xvfb :99 -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
XVFB_PID=$!
trap 'kill ${XVFB_PID} 2>/dev/null || true' EXIT
sleep 1
export DISPLAY=:99 QT_QPA_PLATFORM=xcb

echo "==> zalos --check-deps"
set +e
REPORT=$("${ZALOS}" --check-deps 2>&1)
RC=$?
set -e
echo "${REPORT}" | grep -v -E 'XDG_RUNTIME|propertyCache'
[ ${RC} -eq 0 ] || { echo "❌ a required dependency is missing (exit ${RC})"; exit 1; }
echo "${REPORT}" | grep -E '^audio\.opus +libopus 1\.6\.1$' >/dev/null || { echo "❌ bundled libopus 1.6.1 missing from the call engine"; exit 1; }
if [ "${TARGET}" = "appimage" ] || [ "${TARGET}" = "deb" ] || [ "${TARGET}" = "rpm" ]; then
    echo "${REPORT}" | grep -Eq '^qt +6\.8\.3.*\(bundled\)' || { echo "❌ expected bundled Qt 6.8.3"; exit 1; }
fi

echo "==> zalos --test-gui"
set +e
OUT=$("${ZALOS}" --test-gui 2>&1)
RC=$?
set -e
echo "${OUT}" | grep -v -E 'propertyCache|XDG_RUNTIME' | tail -6
if [ ${RC} -ne 0 ] || echo "${OUT}" | grep -q -E "Failed to load|is not installed|ReferenceError|TypeError|module .* is not installed"; then
    echo "❌ GUI check failed (exit ${RC})"
    exit 1
fi
echo "✅ ${TARGET} on ${PRETTY_NAME}: starts with minimal dependencies"
