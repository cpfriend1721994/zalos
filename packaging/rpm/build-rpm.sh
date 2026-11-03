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
TARBALL_NAME="zalos-1.0.0.tar.gz"
echo "==> Creating source tarball ${TARBALL_NAME}..."
# Only files git tracks or would track (.gitignore respected): never sessions (zsess.json), offline caches
# (.local/, .config/, .cache/ written by containers with HOME=/workspace), build trees or dist/.
( cd "${WORKSPACE_DIR}" &&
  git -c safe.directory='*' ls-files -z --cached --others --exclude-standard |
  while IFS= read -r -d '' f; do [ -f "$f" ] && printf '%s\0' "$f"; done ) > /tmp/zalos-src-files
if [ ! -s /tmp/zalos-src-files ]; then
    echo "❌ git ls-files returned nothing: refusing to tar the whole workspace" >&2
    exit 1
fi
tar -C "${WORKSPACE_DIR}" --transform 's,^,zalos-1.0.0/,' \
    -czf "${RPMBUILD_DIR}/SOURCES/${TARBALL_NAME}" --null -T /tmp/zalos-src-files

# 2. Copy spec file
cp "${WORKSPACE_DIR}/packaging/rpm/zalos.spec" "${RPMBUILD_DIR}/SPECS/"

# 3. Build RPM package
echo "==> Running rpmbuild..."
rpmbuild --define "_topdir ${RPMBUILD_DIR}" -ba "${RPMBUILD_DIR}/SPECS/zalos.spec"

# 4. Copy generated RPMs to dist
cp -v "${RPMBUILD_DIR}/RPMS/"*/*.rpm "${OUTPUT_DIR}/" || true
cp -v "${RPMBUILD_DIR}/SRPMS/"*.src.rpm "${OUTPUT_DIR}/" || true

echo "✅ RPM build step completed. RPMs saved in dist/"

