#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "🧪 Testing Zalos RPM on Clean Fedora Container"
echo "=========================================================="

RPM_FILE=$(ls /workspace/dist/zalos-*.x86_64.rpm 2>/dev/null | head -n 1)
if [ -z "${RPM_FILE}" ]; then
    echo "❌ No RPM found in dist/. Please build it first: podman compose run --rm package-rpm"
    exit 1
fi

echo "==> Found RPM: ${RPM_FILE}"
echo "==> Inspecting RPM metadata and dependencies..."
rpm -qip "${RPM_FILE}"

echo "==> Installing RPM via DNF on clean Fedora $(rpm -E %fedora)..."
dnf install -y "${RPM_FILE}" xorg-x11-server-Xvfb

echo "==> Checking binary location and permissions..."
command -v zalos
ls -la /usr/bin/zalos

echo "==> Verifying dynamic library linking (ldd)..."
ldd /usr/bin/zalos

echo "==> Verifying installed Desktop and Icon files..."
ls -la /usr/share/applications/zalos.desktop
ls -la /usr/share/icons/hicolor/scalable/apps/zalos.svg

echo "==> Running Headless GUI sanity check of installed binary (/usr/bin/zalos)..."
Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
XVFB_PID=$!
sleep 1
DISPLAY=:99 QT_QPA_PLATFORM=xcb /usr/bin/zalos --test-gui
kill ${XVFB_PID}

echo "=========================================================="
echo "✅ Clean Fedora RPM Installation & Execution Passed!"
echo "=========================================================="

