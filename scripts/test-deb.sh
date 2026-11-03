#!/usr/bin/env bash
set -eo pipefail

# Installs the Ubuntu 24.04-built .deb on a clean Ubuntu/Debian image (the service picks the image) and runs the
# headless GUI check of the installed binary.
. /etc/os-release
echo "=========================================================="
echo "🧪 Testing Zalos .deb on clean ${PRETTY_NAME}"
echo "=========================================================="

DEB_FILE=$(ls /workspace/dist/zalos_*~ubuntu24.04_amd64.deb 2>/dev/null | head -n 1)
if [ -z "${DEB_FILE}" ]; then
    echo "❌ No .deb found in dist/. Build it first: podman compose run --rm package-deb"
    exit 1
fi
cp "${DEB_FILE}" /tmp/zalos.deb

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq /tmp/zalos.deb xvfb xauth >/dev/null
dpkg -s zalos | grep -E '^(Version|Depends)'
ldd /usr/bin/zalos | grep -i "not found" && { echo "❌ missing libraries"; exit 1; } || true

echo "==> Headless GUI check of /usr/bin/zalos..."
set +e
OUT=$(HOME=/tmp xvfb-run -a env QT_QPA_PLATFORM=xcb /usr/bin/zalos --test-gui 2>&1)
RC=$?
set -e
echo "${OUT}" | grep -v -E "propertyCache|XDG_RUNTIME" | tail -8
if [ ${RC} -ne 0 ] || echo "${OUT}" | grep -q -E "Failed to load|is not installed|unavailable|ReferenceError|TypeError"; then
    echo "❌ GUI check failed (exit ${RC})"
    exit 1
fi
echo "✅ ${PRETTY_NAME}: .deb installs and starts"
