# syntax=docker/dockerfile:1
FROM docker.io/nixos/nix:latest

LABEL maintainer="Zalos Developers"
LABEL description="Nix (nixos-unstable) build environment for Zalos: builds the Nix package"

ENV NIX_CONFIG="experimental-features = nix-command flakes"

WORKDIR /workspace

CMD ["/bin/bash"]
