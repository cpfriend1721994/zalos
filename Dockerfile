# Fedora base image (one of the two release bases; the other is Dockerfile.ubuntu): development, tests and the RPM.
# Fedora 43 is the oldest Fedora still maintained, so the RPM installs on 43 and every newer release. Bump it when
# 43 reaches end of life (see .agents/skills/04-packaging-infrastructure, "Release matrix").
ARG FEDORA_VERSION=43
FROM docker.io/library/fedora:${FEDORA_VERSION}

LABEL maintainer="Zalos Developers"
LABEL description="Containerized Qt6 development and packaging environment for Zalos"

ENV DEBIAN_FRONTEND=noninteractive
ENV QT_QPA_PLATFORM=offscreen

# Install compiler, build systems, Qt6 dev dependencies, and packaging tools
RUN dnf install -y --setopt=install_weak_deps=False \
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
    # Qt6 Development Libraries
    qt6-qtbase-devel \
    qt6-qtdeclarative-devel \
    qt6-qtsvg-devel \
    qt6-qtimageformats \
    qt6-qtwayland-devel \
    qt6-qtshadertools-devel \
    qt6-qtwebsockets-devel \
    qt6-qtmultimedia-devel \
    # OpenH264 headers only (fedora-cisco-openh264 repo): the library is dlopen()ed from the host at run time
    # and never bundled (Cisco's patent licence covers only binaries users get from Cisco).
    openh264-devel \
    # Cryptographic & Compression Dependencies
    openssl-devel \
    xz-devel \
    zlib-ng-compat-devel \
    sqlite-devel \
    # Packaging & Validation Tools
    rpm-build \
    rpmdevtools \
    desktop-file-utils \
    librsvg2-tools \
    # Headless GUI Testing
    xorg-x11-server-Xvfb \
    mesa-dri-drivers \
    && dnf clean all

WORKDIR /workspace

CMD ["/bin/bash"]

