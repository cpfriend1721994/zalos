#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "❄️ Testing Zalos on Nix / NixOS Container"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
cd "${WORKSPACE_DIR}"

# Safe directory for git inside container
git config --global --add safe.directory "*" || true

# 1. Setup Nix configuration
mkdir -p ~/.config/nix
cat << 'EOF' > ~/.config/nix/nix.conf
experimental-features = nix-command flakes
substituters = https://cache.nixos.org
trusted-public-keys = cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=
EOF

# 2. Check Flake schema
echo "==> Validating Nix flake definition (flake.nix)..."
nix flake metadata
nix flake show

# 3. Build Zalos package via Flake
echo "==> Building Zalos via Nix Flake (nix build .#default)..."
nix build .#default -o /tmp/nix-result

# 4. Inspect Build Artifacts & Qt6 Wrapper
echo "==> Verifying Nix store output..."
NIX_BIN=$(readlink -f /tmp/nix-result/bin/zalos)
echo "Resolved binary in /nix/store: ${NIX_BIN}"
test -x /tmp/nix-result/bin/zalos || { echo "❌ Missing executable /bin/zalos!"; exit 1; }
test -f /tmp/nix-result/share/applications/zalos.desktop || { echo "❌ Missing zalos.desktop!"; exit 1; }
test -f /tmp/nix-result/share/icons/hicolor/scalable/apps/zalos.svg || { echo "❌ Missing zalos.svg!"; exit 1; }

echo "==> Verifying Qt6 environment wrap script..."
head -n 20 /tmp/nix-result/bin/zalos

echo "==> Running command line parser check (--help)..."
QT_QPA_PLATFORM=offscreen /tmp/nix-result/bin/zalos --help

echo "=========================================================="
echo "✅ Nix / NixOS Package Build & Verification Passed!"
echo "=========================================================="
