#!/usr/bin/env bash
# Install/update Zalos into ~/.local (no root needed).
# Usage: ./scripts/install-user.sh [path/to/binary-or-AppImage]
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

SOURCE_INPUT="${1:-}"

if [ -z "${SOURCE_INPUT}" ]; then
    if [ -f "${ROOT_DIR}/build/zalos" ]; then
        SOURCE_INPUT="${ROOT_DIR}/build/zalos"
    elif [ -f "${ROOT_DIR}/dist/Zalos-1.0.7-x86_64.AppImage" ]; then
        SOURCE_INPUT="${ROOT_DIR}/dist/Zalos-1.0.7-x86_64.AppImage"
    elif [ -f "${ROOT_DIR}/dist/Zalos-1.0.6-x86_64.AppImage" ]; then
        SOURCE_INPUT="${ROOT_DIR}/dist/Zalos-1.0.6-x86_64.AppImage"
    fi
fi

if [ -z "${SOURCE_INPUT}" ] || [ ! -f "${SOURCE_INPUT}" ]; then
    echo "❌ Executable binary or AppImage not found."
    echo "Please build it first via podman compose or specify path: ./scripts/install-user.sh <path>"
    exit 1
fi

BIN_DIR="${HOME}/.local/bin"
APP_DIR="${HOME}/.local/share/applications"
ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"

mkdir -p "${BIN_DIR}" "${APP_DIR}" "${ICON_DIR}"

echo "==> Installing Zalos executable from ${SOURCE_INPUT} to ${BIN_DIR}/zalos..."
cp -f "${SOURCE_INPUT}" "${BIN_DIR}/zalos"
chmod +x "${BIN_DIR}/zalos"

echo "==> Installing icon to ${ICON_DIR}/zalos.svg..."
if [ -f "${ROOT_DIR}/assets/icons/zalos.svg" ]; then
    cp -f "${ROOT_DIR}/assets/icons/zalos.svg" "${ICON_DIR}/zalos.svg"
fi

echo "==> Creating desktop entry at ${APP_DIR}/zalos.desktop..."
cat << EOF > "${APP_DIR}/zalos.desktop"
[Desktop Entry]
Name=Zalos
Comment=Zalo desktop client ported to Qt6 (C++/QML) for Linux
Exec=${BIN_DIR}/zalos %u
Icon=zalos
Terminal=false
Type=Application
Categories=Network;InstantMessaging;Chat;Qt;
MimeType=x-scheme-handler/zalo;
StartupWMClass=zalos
Keywords=zalo;chat;im;messaging;
EOF
chmod +x "${APP_DIR}/zalos.desktop"

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "${APP_DIR}" 2>/dev/null || true
fi

echo "==> Verifying installed executable..."
if "${BIN_DIR}/zalos" --help &>/dev/null; then
    echo "✅ Execution self-test verified: ${BIN_DIR}/zalos is operational."
else
    echo "⚠️ Note: Host dependencies check warning. You can test run via container if system Qt libraries differ."
fi

echo "=========================================================="
echo "✅ Zalos installed successfully for current user!"
echo "📍 Binary: ${BIN_DIR}/zalos"
echo "📍 Desktop Entry: ${APP_DIR}/zalos.desktop"
echo "You can launch Zalos from your application launcher or terminal:"
echo "    ${BIN_DIR}/zalos"
echo "=========================================================="
