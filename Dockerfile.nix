# syntax=docker/dockerfile:1
# Nix base image (the third release base, alongside Dockerfile and Dockerfile.ubuntu): builds the Nix package.
# It pins nixos/nix and pre-enables flakes so the packaging job does not have to configure Nix at run time.
# Built multiarch (linux/amd64 + linux/arm64) and pushed to GHCR by base-images.yml, then reused by ci.yml.
FROM docker.io/nixos/nix:latest

LABEL maintainer="Zalos Developers"
LABEL description="Nix (nixos-unstable) build environment for Zalos: builds the Nix package"

# Flakes and the nix-command are required by the Zalos flake; set them once in the image.
ENV NIX_CONFIG="experimental-features = nix-command flakes"

WORKDIR /workspace

CMD ["/bin/bash"]
