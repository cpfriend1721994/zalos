#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Packaging Zalos RPM from the .deb staging tree (no recompile)"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
PKG_DIR="${WORKSPACE_DIR}/build-deb/pkg"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"
RPM_DIST="${ZALOS_RPM_DIST:-.fc43}"
RPMBUILD_DIR="$(mktemp -d /tmp/zalos-rpmbuild.XXXXXX)"
STAGE_DIR="${RPMBUILD_DIR}/stage"
trap 'rm -rf "${RPMBUILD_DIR}"' EXIT

if [ ! -x "${PKG_DIR}/usr/bin/zalos" ] || [ ! -d "${PKG_DIR}/usr/lib/zalos/qt6" ]; then
    echo "❌ ${PKG_DIR} has no staged build: run packaging/deb/build-deb.sh first" >&2
    exit 1
fi
command -v rpmbuild >/dev/null || { echo "❌ rpmbuild not found (install the 'rpm' package)" >&2; exit 1; }

VERSION=$(grep -m1 'project(zalos VERSION' "${WORKSPACE_DIR}/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
case "$(uname -m)" in
    x86_64) RPM_ARCH=x86_64 ;;
    aarch64|arm64) RPM_ARCH=aarch64 ;;
    *) echo "❌ unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

mkdir -p "${STAGE_DIR}" "${RPMBUILD_DIR}/"{BUILD,BUILDROOT,RPMS,SPECS} "${OUTPUT_DIR}"
cp -a "${PKG_DIR}/." "${STAGE_DIR}/"
rm -rf "${STAGE_DIR}/DEBIAN"

echo "==> rpmbuild ${VERSION}-1${RPM_DIST} (${RPM_ARCH})..."
rpmbuild -bb \
    --define "_topdir ${RPMBUILD_DIR}" \
    --define "zalos_version ${VERSION}" \
    --define "zalos_stage ${STAGE_DIR}" \
    --define "dist ${RPM_DIST}" \
    --target "${RPM_ARCH}" \
    "${WORKSPACE_DIR}/packaging/rpm/zalos-prebuilt.spec"

RPM_FILE=$(ls "${RPMBUILD_DIR}/RPMS/${RPM_ARCH}/"zalos-*.rpm)
rpm -qp --requires "${RPM_FILE}" | grep -vE '^(rpmlib|/)' | sort -u
cp -v "${RPM_FILE}" "${OUTPUT_DIR}/"
echo "✅ RPM saved: dist/$(basename "${RPM_FILE}")"
