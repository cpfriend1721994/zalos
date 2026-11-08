#!/usr/bin/env bash
# Vendors the zcall-native call engine (zalo-for-linux, MIT) into third_party/zcall-native.
# Copies only the files git tracks in the upstream checkout, plus its LICENSE, and records the commit in UPSTREAM.
# Usage: scripts/sync-zcall-native.sh [path/to/zalo-for-linux]   (default: ../zalo-for-linux)
# Review the diff afterwards (skill 06-upstream-sync): the engine runs with the user's session data.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="${1:-${REPO_ROOT}/../zalo-for-linux}"
UPSTREAM="$(cd "${UPSTREAM}" && pwd)"
DEST="${REPO_ROOT}/third_party/zcall-native"

if [ -f "${UPSTREAM}/native/engines/zcall/native-engine/zcall-native.js" ]; then
    SRC_SUBDIR="native/engines/zcall"
elif [ -f "${UPSTREAM}/zcall-native/native-engine/zcall-native.js" ]; then
    SRC_SUBDIR="zcall-native"
else
    echo "❌ ${UPSTREAM} has no zcall native-engine/zcall-native.js" >&2
    exit 1
fi

commit="$(git -C "${UPSTREAM}" rev-parse HEAD)"
origin="$(git -C "${UPSTREAM}" remote get-url origin 2>/dev/null | sed -E 's#^git@([^:]+):#https://\1/#; s#\.git$##')"
date="$(git -C "${UPSTREAM}" log -1 --format=%cs -- "${SRC_SUBDIR}")"
dirty="$(git -C "${UPSTREAM}" status --porcelain -- "${SRC_SUBDIR}")"
if [ -n "${dirty}" ]; then
    echo "❌ ${SRC_SUBDIR} has uncommitted changes upstream; commit or stash them first:" >&2
    echo "${dirty}" >&2
    exit 1
fi

rm -rf "${DEST}"
mkdir -p "${DEST}"
git -C "${UPSTREAM}" ls-files -z -- "${SRC_SUBDIR}/README.md" "${SRC_SUBDIR}/native-engine" "${SRC_SUBDIR}/tools" | while IFS= read -r -d '' f; do
    rel="${f#${SRC_SUBDIR}/}"
    mkdir -p "${DEST}/$(dirname "${rel}")"
    cp -p "${UPSTREAM}/${f}" "${DEST}/${rel}"
done
cp "${UPSTREAM}/LICENSE" "${DEST}/LICENSE"
cat > "${DEST}/UPSTREAM" <<EOF
Source:  ${origin:-zalo-for-linux} (${SRC_SUBDIR}/)
Commit:  ${commit}
Date:    ${date}
License: MIT (LICENSE in this folder)
Synced by scripts/sync-zcall-native.sh — do not edit these files here; change them upstream and sync again.
EOF
echo "✅ zcall-native ${commit:0:7} (${date}) → third_party/zcall-native ($(git -C "${UPSTREAM}" ls-files -- "${SRC_SUBDIR}/README.md" "${SRC_SUBDIR}/native-engine" "${SRC_SUBDIR}/tools" | wc -l) files)"
