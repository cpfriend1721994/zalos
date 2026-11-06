#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos RPM Package"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
RPMBUILD_DIR="/tmp/rpmbuild"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"

mkdir -p "${RPMBUILD_DIR}/"{BUILD,RPMS,SOURCES,SPECS,SRPMS} "${OUTPUT_DIR}"

# 1. Create source tarball
TARBALL_NAME="zalos-1.0.6.tar.gz"
echo "==> Creating source tarball ${TARBALL_NAME}..."
# Only project sources, from an explicit exclude list (mirrors .gitignore, .dockerignore and the Nix source
# filter): never sessions (zsess.json), offline caches (.local/, .config/, .cache/), screenshots, build trees,
# packages or agent files. A file list rather than `git ls-files` because the CI packaging image bakes the source
# in with COPY (Dockerfile.app), so there is no repository in the container; the same script still works on a
# bind-mounted checkout locally.
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
tar -C "${WORKSPACE_DIR}" --transform 's,^,zalos-1.0.6/,' \
    -czf "${RPMBUILD_DIR}/SOURCES/${TARBALL_NAME}" --null -T /tmp/zalos-src-files

# 2. Copy spec file
cp "${WORKSPACE_DIR}/packaging/rpm/zalos.spec" "${RPMBUILD_DIR}/SPECS/"

# 3. Build RPM package (binary RPM only; no SRPM needed for release artifacts)
echo "==> Running rpmbuild..."
rpmbuild --define "_topdir ${RPMBUILD_DIR}" -bb "${RPMBUILD_DIR}/SPECS/zalos.spec"

# 4. Copy generated RPMs to dist
cp -v "${RPMBUILD_DIR}/RPMS/"*/*.rpm "${OUTPUT_DIR}/" || true
cp -v "${RPMBUILD_DIR}/SRPMS/"*.src.rpm "${OUTPUT_DIR}/" || true

echo "✅ RPM build step completed. RPMs saved in dist/"

