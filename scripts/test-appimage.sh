#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "🧪 Testing Zalos AppImage"
echo "=========================================================="

APPIMAGE_FILE=$(ls -v /workspace/dist/Zalos-*-x86_64.AppImage 2>/dev/null | tail -n 1)
if [ -z "${APPIMAGE_FILE}" ]; then
    echo "❌ No AppImage found in dist/. Please build it first: podman compose run --rm package-appimage"
    exit 1
fi

echo "==> Found AppImage: ${APPIMAGE_FILE}"
echo "==> File details & permissions:"
ls -lh "${APPIMAGE_FILE}"

echo "==> Extracting AppImage contents for inspection..."
EXTRACT_DIR="/tmp/appimage-extracted"
rm -rf "${EXTRACT_DIR}"
mkdir -p "${EXTRACT_DIR}"
cd "${EXTRACT_DIR}"

"${APPIMAGE_FILE}" --appimage-extract >/dev/null

echo "==> Verifying AppDir structure..."
test -f squashfs-root/AppRun || { echo "❌ Missing AppRun!"; exit 1; }
test -f squashfs-root/zalos.desktop || { echo "❌ Missing zalos.desktop!"; exit 1; }
test -f squashfs-root/usr/bin/zalos || { echo "❌ Missing /usr/bin/zalos!"; exit 1; }
ls -la squashfs-root/AppRun
ls -la squashfs-root/usr/bin/zalos

echo "==> Checking dynamic library linking & QML AOT symbols..."
ldd_clean="$(env -u LD_LIBRARY_PATH ldd squashfs-root/usr/bin/zalos)"
printf '%s\n' "${ldd_clean}" | grep -q "qt6-fallback/lib/libQt6Core.so.6" || { echo "❌ Missing bundled Qt 6.8.3 linking!"; exit 1; }
printf '%s\n' "${ldd_clean}" | grep -q "not found" && { echo "❌ Missing shared library in AppImage!"; exit 1; } || true
grep -aEq "Qt_6\.8_PRIVATE_API|AOTCompiledContext" squashfs-root/usr/bin/zalos || { echo "❌ QML AOT compiled symbols missing!"; exit 1; }
./squashfs-root/AppRun --check-deps | grep -Eq "^qt +6\.8\.3.*\(bundled\)" || { echo "❌ Expected bundled Qt 6.8.3 in --check-deps!"; exit 1; }

echo "==> Running Headless GUI sanity test via virtual display (Xvfb)..."
Xvfb :99 -screen 0 1024x768x24 -nolisten tcp &
XVFB_PID=$!
sleep 1
DISPLAY=:99 QT_QPA_PLATFORM=xcb ./squashfs-root/AppRun --test-gui
kill ${XVFB_PID}

echo "=========================================================="
echo "✅ AppImage Verification & Headless Execution Passed!"
echo "=========================================================="

