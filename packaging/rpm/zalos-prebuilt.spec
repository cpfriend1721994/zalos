%global debug_package %{nil}
%global _binary_payload w9.xzdio
%global __os_install_post %{nil}
%global _build_id_links none
%global __provides_exclude_from ^/usr/lib/zalos/(fallback|qt6)/.*$
%global __requires_exclude_from ^/usr/lib/zalos/(fallback|qt6)/.*$
%global __requires_exclude ^libQt6.*$|^libicu.*$

Name:           zalos
Version:        %{zalos_version}
Release:        1%{?dist}
Summary:        Native Qt6 (C++/QML) messaging client for Zalo on Linux
License:        MIT AND BSD-3-Clause AND OFL-1.1
URL:            https://github.com/cpfriend1721994/zalos

Requires:       mesa-libGL
Requires:       mesa-libEGL
Requires:       fontconfig
Requires:       freetype
Requires:       dbus-libs
Requires:       libxkbcommon
Requires:       libxkbcommon-x11
Requires:       libwayland-client
Requires:       libwayland-cursor
Requires:       libwayland-egl
Requires:       xcb-util-cursor
Requires:       openssl-libs
Requires:       sqlite-libs
Requires:       xz-libs
Requires:       zlib
Requires:       libzstd
Requires:       libpng
Requires:       libicu
Requires:       pulseaudio-libs
Requires:       libXrandr
Requires:       krb5-libs
Recommends:     opus
Recommends:     openh264

%description
Zalos is an open-source native messaging client for Zalo on Linux built with
Qt6 (C++ and QML). It bundles a private Qt 6.8 LTS runtime, supports Wayland
and Xorg, and runs calls on its own C++ call engine. OpenH264 is never bundled.

%install
cp -a %{zalos_stage}/. %{buildroot}/

%files
/usr/bin/zalos
/usr/libexec/zalos
/usr/lib/zalos
%license /usr/share/licenses/zalos/zcall-cpp.LICENSE
%license /usr/share/zalos/fonts/NotoSans-OFL.txt
/usr/share/applications/zalos.desktop
/usr/share/icons/hicolor/scalable/apps/zalos.svg
/usr/share/metainfo/io.github.cpfriend1721994.zalos.metainfo.xml
/usr/share/zalos/fonts/ZalosEmoji-CBDT.ttf
/usr/share/zalos/fonts/NotoSans-VF.ttf
