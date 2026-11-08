%global debug_package %{nil}
%undefine _include_minidebuginfo
%undefine _Include_gdb_index
%global _find_debuginfo_dwz_opts %{nil}

Name:           zalos
Version:        1.0.8
Release:        1%{?dist}
Summary:        Zalo desktop client ported to Qt6 (C++/QML) for Linux
License:        MIT
URL:            https://github.com/cpfriend1721994/zalos
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.22
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel >= 6.4
BuildRequires:  qt6-qtdeclarative-devel >= 6.4
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qtwayland-devel
BuildRequires:  qt6-qtwebsockets-devel
BuildRequires:  qt6-qtmultimedia-devel
BuildRequires:  zlib-devel
BuildRequires:  openssl-devel
BuildRequires:  xz-devel
BuildRequires:  sqlite-devel
BuildRequires:  desktop-file-utils

Requires:       qt6-qtbase >= 6.4
Requires:       qt6-qtdeclarative >= 6.4
Requires:       qt6-qtsvg
Requires:       qt6-qtimageformats
Requires:       qt6-qtwayland
Requires:       qt6-qtwebsockets
Requires:       qt6-qtmultimedia
Requires:       python3
# Bundled zcall-native call engine (zalo-for-linux, MIT) in %{_datadir}/%{name}/zcall-native: node + libopus.
Requires:       nodejs
Requires:       opus
Recommends:     pulseaudio-utils
Requires:       xcb-util-cursor
Requires:       openssl
Requires:       sqlite
# Call video: OpenH264 is dlopen()ed, never bundled (fedora-cisco-openh264 repo).
Recommends:     openh264

%description
Zalos is a high-performance native desktop client for Zalo built with Qt6
(C++ and QML). It supports Wayland and Xorg seamlessly, integrates with GTK
and Qt desktop environments, and includes native cryptographic verification
for E2EE message synchronization.

%prep
%setup -q

%build
%ifarch aarch64
export CFLAGS="$(echo %{build_cflags} | sed 's/-g[a-z0-9=-]*//g')"
export CXXFLAGS="$(echo %{build_cxxflags} | sed 's/-g[a-z0-9=-]*//g') -g0 -DNDEBUG"
%endif
%cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DZALOS_QML_AOT=OFF
%ifarch aarch64
%cmake_build -- -j5
%else
%cmake_build
%endif

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop

%files
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_datadir}/metainfo/io.github.cpfriend1721994.zalos.metainfo.xml
%{_datadir}/%{name}/export_browser_cookies.py
%{_datadir}/%{name}/zcall-native/

%changelog
* Sun Nov 08 2026 Zalos Team <dev@zalos.local> - 1.0.8-1
- Fix unread badge resurrection across restarts via dual seen dispatch and client-side fallback read points.
- High-res cover/avatar photos and layered chat background blur effect with proper layout clipping.
- Speed up aarch64 packaging on CI runners using precompiled headers, DWARF suppression, and increased concurrency.
- Fix AppImage AppStream packaging warnings and self-extract type2 runtime to protect against 504 gateway timeouts.
- Sync upstream zcall-native (callee status code 160) and scope session cookies to Zalo domains.

* Sat Nov 07 2026 Zalos Team <dev@zalos.local> - 1.0.7-1
- Interactive draft attachments bar for pasted clipboard images (Ctrl+V) and multi-file queueing.
- In-app ImageViewer annotation canvas (pen, rect, arrow, text, undo, print, clipboard copy).
- Window geometry persistence, dock unread counter badge, and chat background opacity controls.
- Audio-only answering for incoming video calls, automatic IR webcam filter, and outgoing ringback tone.

* Sat Nov 07 2026 Zalos Team <dev@zalos.local> - 1.0.6-1
- Deduplicate messages by cliMsgId in MessageModel and offline.db (mobile resync duplicates) with a one-shot migration.
- Render Zalo text emoticons (:v, :)), =)), :D, :P ...) as emoji with token boundaries.
- Create the ImageViewer media player on demand so photos never initialise QtMultimedia.
- Minify the GitHub Pages site into _site/ during deployment.

* Fri Nov 06 2026 Zalos Team <dev@zalos.local> - 1.0.5-1
- Telegram Desktop-inspired memory architecture: single-buffer MessageModel, zero-RAM raw_json duplication, scaled QImageReader decode, lazy ConversationInfoPanel, and background tray memory trimming.
- Complete mobile history sync cleanup (cleanMobileSync) and 64-bit znoise resolution for mentions and quotes.
- In-app video player in ImageViewer, clickable avatars/cover photos/chat backgrounds, and reactive avatar-mode chat backgrounds.
- Fix emulated aarch64 Nix OOM (disable QML AOT on aarch64, cap build cores to 2) and silence freopen unused-result warning.

* Fri Nov 06 2026 Zalos Team <dev@zalos.local> - 1.0.4-1
- First published release: bind-mount-free CI packaging, Nix sandbox fix for emulated aarch64.
- Fix outgoing 1-1 video call camera state (zcall-native 2b93852), early file_done race, attachment extensions, and mute sync.
- Optimize build speed and binary size across x86_64 and aarch64.
- Repoint all metadata/homepage to cpfriend1721994 and AppStream id to io.github.cpfriend1721994.zalos.

* Sat Oct 31 2026 Zalos Team <dev@zalos.local> - 1.0.0-1
- Initial Qt6 release with GTK/Qt theming, Wayland/X11 and E2EE support.

