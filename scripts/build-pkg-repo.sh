#!/usr/bin/env bash
set -euo pipefail

INPUT_DIR="${1:?Usage: $0 <input-pkgs-dir> <output-repo-dir>}"
OUT_DIR="${2:?Usage: $0 <input-pkgs-dir> <output-repo-dir>}"
REPO_BASE_URL="${REPO_BASE_URL:-https://cpfriend1721994.github.io/zalos}"
REDIRECT_URL="${REDIRECT_URL:-https://zalos.pages.dev/}"

if [ -n "${GPG_PRIVATE_KEY_FILE:-}" ] && [ -r "${GPG_PRIVATE_KEY_FILE}" ]; then
    GPG_PRIVATE_KEY="$(cat "${GPG_PRIVATE_KEY_FILE}")"
fi
if [ -n "${GPG_PASSPHRASE_FILE:-}" ] && [ -r "${GPG_PASSPHRASE_FILE}" ]; then
    GPG_PASSPHRASE="$(cat "${GPG_PASSPHRASE_FILE}")"
fi

if [ -z "${GPG_PRIVATE_KEY:-}" ]; then
    echo "GPG_PRIVATE_KEY is required" >&2
    exit 1
fi

GNUPGHOME="$(mktemp -d)"
export GNUPGHOME
chmod 700 "${GNUPGHOME}"
trap 'rm -rf "${GNUPGHOME}"' EXIT

printf 'allow-loopback-pinentry\n' > "${GNUPGHOME}/gpg-agent.conf"
GPG_PASS_FILE="${GNUPGHOME}/passphrase"
printf '%s' "${GPG_PASSPHRASE:-}" > "${GPG_PASS_FILE}"
chmod 600 "${GPG_PASS_FILE}"

printf '%s\n' "${GPG_PRIVATE_KEY}" | gpg --batch --pinentry-mode loopback --passphrase-file "${GPG_PASS_FILE}" --import 2>&1
KEY_FP=$(gpg --list-secret-keys --with-colons | awk -F: '/^fpr:/ { print $10; exit }')
if [ -z "${KEY_FP}" ]; then
    echo "Failed to determine GPG key fingerprint" >&2
    exit 1
fi

gpg_sign() {
    gpg --batch --yes --pinentry-mode loopback --passphrase-file "${GPG_PASS_FILE}" "$@"
}

mkdir -p "${OUT_DIR}"
gpg --armor --export "${KEY_FP}" > "${OUT_DIR}/zalos.gpg"

cat > "${OUT_DIR}/zalos.repo" <<EOF
[zalos]
name=Zalos Repository
baseurl=${REPO_BASE_URL}/rpm/\$basearch
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=${REPO_BASE_URL}/zalos.gpg
EOF

cat > "${OUT_DIR}/index.html" <<EOF
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="0; url=${REDIRECT_URL}">
  <title>Zalos Package Repository</title>
</head>
<body>
  <p>Redirecting to <a href="${REDIRECT_URL}">${REDIRECT_URL}</a>...</p>
</body>
</html>
EOF

APT_DIR="${OUT_DIR}/apt"
mkdir -p "${APT_DIR}/pool/main"
find "${INPUT_DIR}" -maxdepth 2 -type f -name '*.deb' -exec cp -f {} "${APT_DIR}/pool/main/" \;

shopt -s nullglob
DEB_FILES=("${APT_DIR}/pool/main/"*.deb)
if [ "${#DEB_FILES[@]}" -gt 0 ]; then
    for arch in amd64 arm64; do
        BIN_DIR="${APT_DIR}/dists/stable/main/binary-${arch}"
        mkdir -p "${BIN_DIR}"
        (cd "${APT_DIR}" && dpkg-scanpackages --multiversion --arch "${arch}" pool/main) > "${BIN_DIR}/Packages"
        gzip -9c "${BIN_DIR}/Packages" > "${BIN_DIR}/Packages.gz"
    done

    apt-ftparchive \
        -o APT::FTPArchive::Release::Origin="Zalos" \
        -o APT::FTPArchive::Release::Label="Zalos" \
        -o APT::FTPArchive::Release::Suite="stable" \
        -o APT::FTPArchive::Release::Codename="stable" \
        -o APT::FTPArchive::Release::Architectures="amd64 arm64" \
        -o APT::FTPArchive::Release::Components="main" \
        release "${APT_DIR}/dists/stable" > "${APT_DIR}/dists/stable/Release"

    gpg_sign -abs -u "${KEY_FP}" -o "${APT_DIR}/dists/stable/Release.gpg" "${APT_DIR}/dists/stable/Release"
    gpg_sign --clearsign -u "${KEY_FP}" -o "${APT_DIR}/dists/stable/InRelease" "${APT_DIR}/dists/stable/Release"
fi

GPG_WRAPPER="${GNUPGHOME}/gpg-rpm.sh"
cat > "${GPG_WRAPPER}" <<EOF
#!/usr/bin/env bash
exec gpg --batch --no-verbose --no-armor --pinentry-mode loopback --passphrase-file "${GPG_PASS_FILE}" "\$@"
EOF
chmod +x "${GPG_WRAPPER}"

for arch in x86_64 aarch64; do
    RPM_ARCH_DIR="${OUT_DIR}/rpm/${arch}"
    mkdir -p "${RPM_ARCH_DIR}/Packages"
    find "${INPUT_DIR}" -maxdepth 2 -type f -name "*.${arch}.rpm" -exec cp -f {} "${RPM_ARCH_DIR}/Packages/" \;

    RPM_FILES=("${RPM_ARCH_DIR}/Packages/"*.rpm)
    if [ "${#RPM_FILES[@]}" -gt 0 ]; then
        rpmsign \
            --define "_gpg_name ${KEY_FP}" \
            --define "__gpg ${GPG_WRAPPER}" \
            --addsign "${RPM_ARCH_DIR}"/Packages/*.rpm
        createrepo_c "${RPM_ARCH_DIR}"
        gpg_sign --detach-sign --armor -u "${KEY_FP}" "${RPM_ARCH_DIR}/repodata/repomd.xml"
    fi
done
