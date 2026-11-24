#!/usr/bin/env bash
set -eo pipefail

. /etc/os-release
echo "=========================================================="
echo "🧪 Testing Zalos .deb on clean ${PRETTY_NAME}"
echo "=========================================================="

DEB_FILE=$(ls -v /workspace/dist/zalos_*~ubuntu24.04_amd64.deb 2>/dev/null | tail -n 1)
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
grep -aEq "Qt_6\.8_PRIVATE_API|AOTCompiledContext" /usr/bin/zalos || { echo "❌ QML AOT symbols missing!"; exit 1; }
/usr/bin/zalos --check-deps | grep -Eq "^qt +6\.8\.3.*\(bundled\)" || { echo "❌ Expected bundled Qt 6.8.3 in --check-deps!"; exit 1; }

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
