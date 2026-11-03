#!/usr/bin/env bash
set -eo pipefail

# Installs the makepkg-built package on a clean Arch container and runs the headless GUI check.
echo "=========================================================="
echo "🧪 Testing Zalos package on clean Arch Linux"
echo "=========================================================="

PKG_FILE=$(ls /workspace/dist/zalos-*-x86_64.pkg.tar.zst 2>/dev/null | head -n 1)
if [ -z "${PKG_FILE}" ]; then
    echo "❌ No Arch package in dist/. Build it first: podman compose run --rm package-arch"
    exit 1
fi
pacman -Syu --noconfirm >/dev/null
pacman -U --noconfirm "${PKG_FILE}" >/dev/null
pacman -S --noconfirm --needed xorg-server-xvfb xorg-xauth >/dev/null
pacman -Qi zalos | grep -E '^(Version|Depends On)'

set +e
OUT=$(HOME=/tmp xvfb-run -a env QT_QPA_PLATFORM=xcb /usr/bin/zalos --test-gui 2>&1)
RC=$?
set -e
echo "${OUT}" | grep -v -E "propertyCache|XDG_RUNTIME" | tail -8
if [ ${RC} -ne 0 ] || echo "${OUT}" | grep -q -E "Failed to load|is not installed|unavailable|ReferenceError|TypeError"; then
    echo "❌ GUI check failed (exit ${RC})"
    exit 1
fi
echo "✅ Arch Linux: package installs and starts"
