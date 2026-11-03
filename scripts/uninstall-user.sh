#!/usr/bin/env bash
# Uninstall Zalos from ~/.local
set -eo pipefail

BIN="${HOME}/.local/bin/zalos"
DESKTOP="${HOME}/.local/share/applications/zalos.desktop"
ICON="${HOME}/.local/share/icons/hicolor/scalable/apps/zalos.svg"

rm -f "${BIN}" "${DESKTOP}" "${ICON}"

if command -v update-desktop-database &>/dev/null; then
    update-desktop-database "${HOME}/.local/share/applications" 2>/dev/null || true
fi

echo "✅ Zalos has been uninstalled from current user environment (~/.local)."

