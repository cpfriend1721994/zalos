#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos RPM Package"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
RPMBUILD_DIR="/tmp/rpmbuild"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"

mkdir -p "${RPMBUILD_DIR}/"{BUILD,RPMS,SOURCES,SPECS,SRPMS} "${OUTPUT_DIR}"

VERSION=$(grep -m1 'project(zalos VERSION' "${WORKSPACE_DIR}/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
TARBALL_NAME="zalos-${VERSION}.tar.gz"
echo "==> Creating source tarball ${TARBALL_NAME}..."
( cd "${WORKSPACE_DIR}" &&
  find . -mindepth 1 \
      \( -path './.git' -o -path './.agents' -o -path './build' -o -path './build-*' -o -path './dist' \
         -o -path './dist-*' \
         -o -path './.cache' -o -path './.config' -o -path './.local' -o -path './.flatpak-builder' \
         -o -name '__pycache__' \) -prune -o \
      -type f \
      ! -name 'AGENTS.md' ! -name 'CLAUDE.md' ! -name 'GEMINI.md' \
      ! -name 'zsess.json' ! -name '*.session.json' ! -name 'zalos_*.png' \
      ! -name '*.AppImage' ! -name '*.rpm' ! -name '*.deb' ! -name '*.flatpak' ! -name '*.tar.gz' \
      ! -name 'CMakeLists.txt.user' ! -name '*.autosave' ! -name '*.pyc' \
      -printf '%P\0' ) > /tmp/zalos-src-files
if [ ! -s /tmp/zalos-src-files ] || ! tr '\0' '\n' < /tmp/zalos-src-files | grep -qx 'CMakeLists.txt'; then
    echo "❌ Source list is empty or has no CMakeLists.txt: refusing to tar ${WORKSPACE_DIR}" >&2
    exit 1
fi
tar -C "${WORKSPACE_DIR}" --transform "s,^,zalos-${VERSION}/," \
    -czf "${RPMBUILD_DIR}/SOURCES/${TARBALL_NAME}" --null -T /tmp/zalos-src-files

cp "${WORKSPACE_DIR}/packaging/rpm/zalos.spec" "${RPMBUILD_DIR}/SPECS/"

echo "==> Running rpmbuild..."
rpmbuild --define "_topdir ${RPMBUILD_DIR}" -bb "${RPMBUILD_DIR}/SPECS/zalos.spec"

cp -v "${RPMBUILD_DIR}/RPMS/"*/*.rpm "${OUTPUT_DIR}/" || true
cp -v "${RPMBUILD_DIR}/SRPMS/"*.src.rpm "${OUTPUT_DIR}/" || true

echo "✅ RPM build step completed. RPMs saved in dist/"

