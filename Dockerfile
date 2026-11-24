# syntax=docker/dockerfile:1
ARG FEDORA_VERSION=43
FROM docker.io/library/fedora:${FEDORA_VERSION}

ARG TARGETARCH

LABEL maintainer="Zalos Developers"
LABEL description="Containerized Qt 6.8.3 LTS development and packaging environment (Fedora 43 base) for Zalos"

ENV QT_QPA_PLATFORM=offscreen

RUN --mount=type=cache,target=/var/cache/libdnf5,sharing=locked \
    --mount=type=cache,target=/var/cache/dnf,sharing=locked \
    dnf install -y \
    --setopt=install_weak_deps=False \
    --setopt=tsflags=nodocs \
    --setopt=keepcache=1 \
    gcc-c++ \
    make \
    cmake \
    ninja-build \
    git \
    curl \
    tar \
    gzip \
    xz \
    file \
    binutils \
    patchelf \
    python3 \
    python3-pip \
    p7zip \
    pkgconf-pkg-config \
    mesa-libGL-devel \
    mesa-libEGL-devel \
    libxkbcommon-devel \
    libxkbcommon-x11-devel \
    wayland-devel \
    dbus-devel \
    fontconfig-devel \
    freetype-devel \
    libxcb-devel \
    xcb-util-cursor \
    xcb-util-wm \
    xcb-util-image \
    xcb-util-keysyms \
    xcb-util-renderutil \
    openh264-devel \
    openssl-devel \
    xz-devel \
    zlib-ng-compat-devel \
    sqlite-devel \
    libzstd-devel \
    opus \
    pulseaudio-libs \
    libXrandr \
    rpm-build \
    rpmdevtools \
    desktop-file-utils \
    librsvg2-tools \
    xorg-x11-server-Xvfb \
    mesa-dri-drivers

RUN pip3 install --no-cache-dir aqtinstall \
    && ARCH_EFF="${TARGETARCH:-$(uname -m)}" \
    && if [ "${ARCH_EFF}" = "arm64" ] || [ "${ARCH_EFF}" = "aarch64" ]; then \
         aqt install-qt linux_arm64 desktop 6.8.3 linux_gcc_arm64 -O /opt/qt \
           -m qtimageformats qtmultimedia qtshadertools qtwebsockets; \
       else \
         aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/qt \
           -m qtimageformats qtmultimedia qtshadertools qtwebsockets; \
       fi \
    && ln -sfn /opt/qt/6.8.3/gcc_* /opt/qt6 \
    && pip3 uninstall -y aqtinstall || true

RUN curl -fsSL https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz -o /tmp/ffmpeg.tar.xz \
    && mkdir -p /tmp/ff-src && tar -xf /tmp/ffmpeg.tar.xz -C /tmp/ff-src --strip-components=1 \
    && cd /tmp/ff-src \
    && ./configure --prefix=/tmp/ff-out \
        --disable-everything --disable-programs --disable-doc \
        --disable-avdevice --disable-avfilter --disable-postproc \
        --disable-x86asm --disable-autodetect --enable-zlib \
        --enable-shared --disable-static --enable-small \
        --enable-swresample --enable-swscale \
        --enable-protocol=file,pipe,data \
        --enable-demuxer=mov,matroska,ogg,wav,mp3,aac,flac,image2 \
        --enable-muxer=mp4,ipod,ogg,wav,adts \
        --enable-decoder=aac,opus,vorbis,mp3,mp3float,flac,pcm_s16le,pcm_f32le,pcm_s16be,pcm_u8,h264,vp8,vp9,mjpeg,png,webp \
        --enable-encoder=aac,opus,pcm_s16le \
        --enable-parser=aac,opus,vorbis,mpegaudio,h264,vp8,vp9,mjpeg \
        --enable-bsf=aac_adtstoasc \
    && make -j"$(nproc)" && make install-libs \
    && rm -f /opt/qt6/lib/libav*.so* /opt/qt6/lib/libsw*.so* /opt/qt6/lib/libQt6FFmpegStub-*.so* \
    && cp -a /tmp/ff-out/lib/libav*.so* /tmp/ff-out/lib/libsw*.so* /opt/qt6/lib/ \
    && LDD_CHK="$(LD_LIBRARY_PATH=/opt/qt6/lib ldd -r /opt/qt6/plugins/multimedia/libffmpegmediaplugin.so 2>&1)" \
    && if printf '%s\n' "${LDD_CHK}" | grep -qE "not found|undefined symbol"; then printf '%s\n' "${LDD_CHK}" >&2; exit 1; fi \
    && rm -rf /tmp/ffmpeg.tar.xz /tmp/ff-src /tmp/ff-out

RUN pip3 install --no-cache-dir aqtinstall \
    && aqt install-src linux 6.8.3 --archives qtbase -O /tmp/qt-src \
    && LD_LIBRARY_PATH=/opt/qt6/lib /opt/qt6/bin/qt-cmake -G Ninja \
        -S /tmp/qt-src/6.8.3/Src/qtbase/src/plugins/sqldrivers -B /tmp/qsql-build \
        -DCMAKE_BUILD_TYPE=Release -DFEATURE_system_sqlite=ON \
        -DFEATURE_sql_odbc=OFF -DFEATURE_sql_psql=OFF -DFEATURE_sql_mysql=OFF -DFEATURE_sql_ibase=OFF \
        -DFEATURE_sql_oci=OFF -DFEATURE_sql_db2=OFF -DFEATURE_sql_mimer=OFF \
    && LD_LIBRARY_PATH=/opt/qt6/lib cmake --build /tmp/qsql-build --parallel "$(nproc)" \
    && SQLITE_PLUGIN="$(find /tmp/qsql-build -name libqsqlite.so -type f | head -n 1)" \
    && test -n "${SQLITE_PLUGIN}" \
    && LD_LIBRARY_PATH=/opt/qt6/lib ldd "${SQLITE_PLUGIN}" | grep -q 'libsqlite3\.so' \
    && strip --strip-unneeded "${SQLITE_PLUGIN}" \
    && install -m 0755 "${SQLITE_PLUGIN}" /opt/qt6/plugins/sqldrivers/libqsqlite.so \
    && pip3 uninstall -y aqtinstall \
    && rm -rf /tmp/qt-src /tmp/qsql-build

ENV QT_ROOT=/opt/qt6
ENV Qt6_DIR=/opt/qt6/lib/cmake/Qt6
ENV CMAKE_PREFIX_PATH=/opt/qt6
ENV PATH="/opt/qt6/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/qt6/lib"

WORKDIR /workspace

CMD ["/bin/bash"]
