#!/usr/bin/env bash
set -eo pipefail

TAG="${1:?usage: ci-release-notes.sh <tag> [output-file]}"
OUT="${2:-release-notes.md}"

CHANGELOG="${CHANGELOG:-CHANGELOG.md}"
REPO="${GITHUB_REPOSITORY:-cpfriend1721994/zalos}"
WEBSITE="https://zalos.pages.dev/"

VERSION="${TAG#v}"

if [ ! -f "${CHANGELOG}" ]; then
    echo "❌ ${CHANGELOG} not found; cannot build release notes" >&2
    exit 1
fi

SECTION="$(awk -v v="${VERSION}" '
    $0 ~ "^## \\[" v "\\]" { grab = 1; next }
    grab && /^## \[/ { exit }
    grab { print }
' "${CHANGELOG}" | awk 'NF { p = 1 } p' )"
SECTION="${SECTION%"${SECTION##*[![:space:]]}"}"

if [ -z "${SECTION}" ]; then
    echo "⚠️  CHANGELOG.md has no '## [${VERSION}]' section — release body will lack changelog text." >&2
    SECTION="_No curated changelog entry for ${VERSION} yet — see the full comparison below._"
fi

PREV="$(git describe --tags --abbrev=0 "${TAG}^" 2>/dev/null || true)"

{
    echo "> 🌐 Website: <${WEBSITE}> · Install guide: \`README.md\` / \`README.vi.md\`"
    echo
    echo "${SECTION}"
    echo
    echo "### 📦 Packages (each ships x86_64 **and** aarch64/ARM64 — match your \`uname -m\`)"
    echo
    echo "| Format | For | Asset |"
    echo "|---|---|---|"
    echo "| AppImage | openSUSE, Debian, Arch and most other distros | \`Zalos-${VERSION}-x86_64.AppImage\` · \`Zalos-${VERSION}-aarch64.AppImage\` |"
    echo "| .deb | Ubuntu 24.04+ and derivatives | \`zalos_${VERSION}-1~ubuntu24.04_amd64.deb\` · \`..._arm64.deb\` |"
    echo "| RPM | Fedora 43+ | \`zalos-${VERSION}-1.fc43.x86_64.rpm\` · \`zalos-${VERSION}-1.fc43.aarch64.rpm\` |"
    echo "| Nix | NixOS / any distro with Nix | \`zalos-${VERSION}-x86_64-nixos.tar.gz\` · \`...-aarch64-nixos.tar.gz\` |"
    echo
    echo "Checksums: \`SHA256SUMS\`."
    if [ -n "${PREV}" ]; then
        echo
        echo "**Full Changelog**: <https://github.com/${REPO}/compare/${PREV}...${TAG}>"
    fi
} > "${OUT}"

echo "===== ${OUT} ====="
cat "${OUT}"
