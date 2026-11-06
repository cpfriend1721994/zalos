#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "❄️ Building Zalos for NixOS / Nix"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"
mkdir -p "${OUTPUT_DIR}"

cd "${WORKSPACE_DIR}"

# Safe directory for git inside container
git config --global --add safe.directory "*" || true

# Enable flakes and nix-command in nix.conf.
# The Nix sandbox filters syscalls with seccomp, and a seccomp BPF program is only accepted for the architecture of
# the *kernel*: in an aarch64 container emulated by QEMU on an x86_64 host, loading it fails with
# "unable to load seccomp BPF program: Invalid argument" and the build dies while setting up the environment. So an
# emulated run (ZALOS_EMULATED=1, set by scripts/ci-package.sh and by the package-*-arm64 compose services) turns
# the sandbox off and caps Ninja parallelism (cores = 2) so QEMU user-mode emulation + GCC compiling large C++/Qt6
# translation units cannot exhaust the CI runner's RAM and trigger the kernel OOM-killer (exit code 137 / SIGKILL).
mkdir -p ~/.config/nix
NIX_SANDBOX=true
NIX_CORES=0
if [ "${ZALOS_EMULATED:-0}" = "1" ]; then
    NIX_SANDBOX=false
    NIX_CORES=2
    echo "==> Emulated architecture detected: disabling the Nix sandbox and capping cores=${NIX_CORES}"
fi
cat << EOF > ~/.config/nix/nix.conf
experimental-features = nix-command flakes
substituters = https://cache.nixos.org
trusted-public-keys = cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=
sandbox = ${NIX_SANDBOX}
filter-syscalls = ${NIX_SANDBOX}
cores = ${NIX_CORES}
EOF

echo "==> Building Zalos package via default.nix..."
if command -v nix &>/dev/null; then
    # Newest nixos-unstable (Qt, OpenSSL, OpenH264, ...) pinned in flake.lock, then `nix-build default.nix` with
    # that nixpkgs: only the filtered source reaches the store (`nix build path:.` would copy the whole directory,
    # sessions and caches included, and a git flake would miss untracked files).
    if [ "${ZALOS_EMULATED:-0}" != "1" ] && [ "${ZALOS_SKIP_FLAKE_UPDATE:-0}" != "1" ]; then
        echo "==> Updating flake.lock to the newest nixos-unstable..."
        nix flake update --extra-experimental-features "nix-command flakes"
    else
        echo "==> Using existing flake.lock..."
    fi
    NIXPKGS_REV=$(python3 -c 'import json;print(json.load(open("flake.lock"))["nodes"]["nixpkgs"]["locked"]["rev"])' 2>/dev/null ||
                  nix eval --raw --impure --expr '(builtins.fromJSON (builtins.readFile ./flake.lock)).nodes.nixpkgs.locked.rev')
    NIXPKGS_HASH=$(nix eval --raw --impure --expr '(builtins.fromJSON (builtins.readFile ./flake.lock)).nodes.nixpkgs.locked.narHash')
    echo "==> nixpkgs ${NIXPKGS_REV}"
    nix-build -o /tmp/nix-result -E "import ./default.nix { pkgs = import (builtins.fetchTarball {
        url = \"https://github.com/NixOS/nixpkgs/archive/${NIXPKGS_REV}.tar.gz\"; sha256 = \"${NIXPKGS_HASH}\"; }) {}; }"
    tar -czf "${OUTPUT_DIR}/zalos-1.0.5-$(uname -m)-nixos.tar.gz" -C /tmp/nix-result .
    echo "✅ Nix package archive saved: dist/zalos-1.0.5-$(uname -m)-nixos.tar.gz"
else
    echo "❌ Neither nix-build nor nix command found."
    exit 1
fi

echo "✅ NixOS build step completed successfully."
