# syntax=docker/dockerfile:1
# Fedora base image (one of the two release bases; the other is Dockerfile.ubuntu): development, tests and the RPM.
# Fedora 43 is the oldest Fedora still maintained, so the RPM installs on 43 and every newer release. Bump it when
# 43 reaches end of life.
ARG FEDORA_VERSION=43
FROM docker.io/library/fedora:${FEDORA_VERSION}

LABEL maintainer="Zalos Developers"
LABEL description="Containerized Qt6 development and packaging environment for Zalos"

ENV QT_QPA_PLATFORM=offscreen

# One install layer, with the dnf download cache kept on a BuildKit cache mount (persists on the builder, never
# baked into the image). nodocs + no weak deps keep the image small. No `dnf clean all` is needed: the cache lives
# in the mount, not in the layer.
RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked \
    --mount=type=cache,target=/var/cache/dnf,sharing=locked \
    dnf install -y \
    --setopt=install_weak_deps=False \
    --setopt=tsflags=nodocs \
    --setopt=keepcache=1 \
    gcc-c++ \
    cmake \
    ninja-build \
    git \
    curl \
    tar \
    gzip \
    xz \
    file \
    pkgconf-pkg-config \
    qt6-qtbase-devel \
    qt6-qtdeclarative-devel \
    qt6-qtsvg-devel \
    qt6-qtimageformats \
    qt6-qtwayland-devel \
    qt6-qtshadertools-devel \
    qt6-qtwebsockets-devel \
    qt6-qtmultimedia-devel \
    openh264-devel \
    openssl-devel \
    xz-devel \
    zlib-ng-compat-devel \
    sqlite-devel \
    rpm-build \
    rpmdevtools \
    desktop-file-utils \
    librsvg2-tools \
    xorg-x11-server-Xvfb \
    mesa-dri-drivers

WORKDIR /workspace

CMD ["/bin/bash"]
