%global debug_package %{nil}
%global _binary_payload w9.xzdio
%undefine _include_minidebuginfo
%undefine _include_gdb_index
%global _find_debuginfo_dwz_opts %{nil}

Name:           zalos
Version:        1.3.0
Release:        1%{?dist}
Summary:        Native Qt6 (C++/QML) messaging client for Zalo on Linux
License:        MIT AND BSD-3-Clause AND OFL-1.1
URL:            https://github.com/cpfriend1721994/zalos
Source0:        %{name}-%{version}.tar.gz

%global __provides_exclude_from ^%{_libdir}/%{name}/(fallback|qt6)/.*$
%global __requires_exclude_from ^%{_libdir}/%{name}/(fallback|qt6)/.*$
%global __requires_exclude ^libQt6.*$|^libicu.*$

BuildRequires:  cmake >= 3.22
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  pkgconf-pkg-config
BuildRequires:  openh264-devel
BuildRequires:  zlib-devel
BuildRequires:  openssl-devel
BuildRequires:  xz-devel
BuildRequires:  sqlite-devel
BuildRequires:  desktop-file-utils
BuildRequires:  opus

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
Zalos is an open-source, high-performance native messaging client
for Zalo on Linux built with Qt6 (C++ and QML). It supports Wayland
and Xorg seamlessly, integrates with GTK and Qt desktop environments, and
includes native cryptographic verification for E2EE message synchronization.

%prep
%setup -q

%build
%ifarch aarch64
export CFLAGS="$(echo %{build_cflags} | sed 's/-g[a-z0-9=-]*//g')"
export CXXFLAGS="$(echo %{build_cxxflags} | sed 's/-g[a-z0-9=-]*//g') -g0 -DNDEBUG"
%endif
%cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DZALOS_QML_AOT=ON
%ifarch aarch64
%cmake_build -- -j5
%else
%cmake_build
%endif

%install
%cmake_install
bash packaging/common/bundle-qt6.sh /opt/qt6 %{buildroot}%{_libdir}/%{name}/qt6 %{buildroot}%{_bindir} %{buildroot}%{_libexecdir}/%{name} --build-icu-shim
install -d %{buildroot}%{_libdir}/%{name}/fallback
install -m 0644 "$(readlink -f %{_libdir}/libopus.so.0)" %{buildroot}%{_libdir}/%{name}/fallback/libopus.so.0

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop

%files
%{_bindir}/%{name}
%{_libexecdir}/%{name}/zalos-call
%{_libexecdir}/%{name}/zalos-call-audio
%{_libdir}/%{name}/fallback/libopus.so.0
%{_libdir}/%{name}/qt6
%license %{_datadir}/licenses/%{name}/zcall-cpp.LICENSE
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_datadir}/metainfo/io.github.cpfriend1721994.zalos.metainfo.xml
%{_datadir}/%{name}/fonts/ZalosEmoji-CBDT.ttf
%{_datadir}/%{name}/fonts/NotoSans-VF.ttf
%license %{_datadir}/%{name}/fonts/NotoSans-OFL.txt

%changelog
* Tue Nov 24 2026 Zalos Team <dev@zalos.local> - 1.3.0-1
- Wire zBusiness Tools to live server APIs (business profile lookup, product catalog & item CRUD, and auto-reply rules) alongside existing quick-message shortcuts.
- Add Zalo-style OpenStreetMap tile preview cards with sender avatar pins for shared location messages and fix wide panoramic image sizing/aspect fit.
- Persist labels in SQLite offline cache for instant startup display, fix navigation rail label rendering and hover stability, and fix desktop System Theme colors and KDE/GNOME font detection.
- Improve narrow-window (480x440+) responsive layouts, modal Esc/keyboard navigation, DPR image decoding bounds, media-run capping, async sound/clipboard handling, and bundle Qt 6.8.3 LTS with QML AOT compilation cleanly.
- Fix the UI font falling back to the emoji font when the desktop font is missing (Noto Sans fallback, bundled under OFL), links that failed to open or were not clickable, and zBusiness catalog error 113.
- Use native portal file dialogs instead of Qt Quick Dialogs and the system SQLite for the bundled SQL driver; RPMs are now repackaged from the Ubuntu 24.04 build.

* Mon Nov 23 2026 Zalos Team <dev@zalos.local> - 1.2.10-1
- Fix being stuck on "Connecting to server" with WebSocket error 501 on Qt 6.4 (the realtime URL now always has a "/" path).
- Fix a crash in rounded images when the device pixel ratio changes on Wayland.
- Stop the AppImage from falling back to the bundled Qt after a normal exit (--version, --check-deps, second instance).
- Fix the chat jumping back to the bottom while scrolling up as photos load or messages arrive.
- Start in English until a language is chosen and save the language picked on the sign-in screen.
- Use the system Qt in the AppImage only from 6.4 to 6.12.
- Keep 1-1 call relay servers alive while ringing, recover audio across Wi-Fi/VPN changes or P2P drops (ChangeAddress, P2P stop, multi-ROC SRTP fallback, packet deduplication, jitter buffer reset), and add an Auto/Manual microphone gain toggle in the call device menu.
- Improve Sync v2 phone history transfer with two-phase conversation/message batches, idle-phone wake-up (cmd 592), and quote/mention/TTL metadata, plus group_poll host fallback and quote reply owner/cliMsgId normalization.
- Fix missing images when opening conversations after reading photo+text sequences on mobile (getrecentv2-to-getoldv2 chaining, stringified/alternate chat.photo parsing, 1-1/post-login/reconnect history sync, group_cloud_message host fallback, and offline cache reparse).

* Fri Nov 20 2026 Zalos Team <dev@zalos.local> - 1.2.9-1
- Add Zalo PC-style polls: info lines for every poll event, card under the latest event, full vote dialog (single/multiple choice, change or remove votes, add options, voters per option, close and share), all creation settings with Zalo's limits, and pinned polls.
- Fix chats read on the phone staying unread (persistent per-conversation read points applied in order, get-last-seen sync after reconnect/window show/every 15 minutes).
- Fix phone history sync failing with code -1 when the phone app goes to the background after confirming.
- Fix poll votes replacing earlier choices, double votes when adding options, and the placeholder poll card; release viewer memory on close and stop caching full-size photos; fix Window attached-property QML warnings.
- Default to software rendering with automatic fallback to software when a hardware renderer fails.
- Require fewer system packages (libopus, PulseAudio and OpenH264 are recommendations; a private libopus copy is used only when the system has none), read the browser key over D-Bus, keep working without Qt Multimedia codecs, and add zalos --check-deps.

* Thu Nov 19 2026 Zalos Team <dev@zalos.local> - 1.2.8-1
- Fix call hangup not ending remote calls (clear both incoming callbacks on accept/reject/non-incoming state, pre-UI-socket hangup cancellation, sendUi flush, >4 MiB video guard, UDP EndCall on pre-answer cancel, and omitted undefined fields on group end/cancel).
- Fix group chat sidebar previews (plus desktop notifications, birthday ecards, and offline SQLite startup backfill) missing sender names with incremental updates on async profile or alias resolution.
- Fix image clicks in chat and Info Panel not opening ImageViewer; cap fetchBytes at 64 MiB and keep static WebP images non-animated.
- Optimize CPU, RAM, and I/O across Qt/QML and zcall-cpp (batched SQLite writes, off-thread >32 KiB API decrypt, P2P verification, thread_local SRTP HMAC, native YUV/NV12/YUYV camera paths, paused hidden spinners, and single-session PulseAudio probe).

* Wed Nov 18 2026 Zalos Team <dev@zalos.local> - 1.2.7-1
- Add phone history sync v2 fallback (beta, OpenSSL 3 + Protobuf + cmd 590/591), flexible libzstd linking, Qt 6.4 QML fixes, 1-1 screen sharing, PiP/tray call controls, animated GIF playback, and Wayland IME/retry hardening.

* Mon Nov 16 2026 Zalos Team <dev@zalos.local> - 1.2.6-1
- Add group invite box, friend recommendations, mutual groups, E2EE linked devices, safe link scanning, abuse reporting, Community upgrade, bank card REST APIs, and memory/image decoder hardening.

* Mon Nov 16 2026 Zalos Team <dev@zalos.local> - 1.2.5-1
- Add in-app update support (AppImage SHA-256 self-update + gh-releases-zsync, .deb/.rpm pkexec install), conversation type filter, smart mobile sync prompt, Settings About & GPU toggle restart, and 90% QML AOT coverage.

* Sun Nov 15 2026 Zalos Team <dev@zalos.local> - 1.2.4-1
- Replace pactl/pacat subprocesses with direct libpulse-simple dlopen(), bundle libopus.so.0 fallback in AppImage, and detach AppImage cleanly from launching terminals.
- Expand Emoji Picker (1,000+ emojis with scroll-all tabs), reactions (54 unique), emoticon shortcodes (86), and full Sticker catalog with live debounced search.
- Sync upstream Zalo Web API_VERSION 695, /api/cm/getrecentv2 + /api/cm/getoldv2 cloud paging, lenient profile fallback, and sendLink->sendText fallback.

* Sun Nov 15 2026 Zalos Team <dev@zalos.local> - 1.2.3-1
- Self-contained AppImage with surgical Qt 6.4 fallback, glibc 2.35 compatibility shim, dynamic host-ICU/libproxy forwarding, and zstd -22 compression (~22 MB).
- Fix Qt 6.4 QML compatibility (remove read-only AnimatedImage.sourceSize and pragma ComponentBehavior: Bound in Loader files).
- Canonical MIT LICENSE, AppStream metainfo validation, and no auto-detach in AppRun.

* Sun Nov 15 2026 Zalos Team <dev@zalos.local> - 1.2.2-1
- Release GPU scene-graph/textures on tray hide, add MADV_COLD + PR_SET_MEMORY_MERGE + GLIBC_TUNABLES, filter minimal Qt plugin path, lazily load QtMultimedia/FFmpeg and self-heal RenderGuard v2.
- Pause animations, carousel, clock and presence polling while hidden; single-shot MessageScheduler, jittered WebSocket reconnect backoff, debounced contact refresh and versioned group sync.
- Asynchronous portal theming, lazy sticker store, incremental ContactModel, virtualized group member ListView, cached C++ @mentions, split Avatar/Media RAM caches and static WebP detection.
- Streaming 64 KiB ZDB4 decryption, background-thread mobile sync & search, indexed messageTs, optimal 640x360 camera format selection, post-answer video encoding, active-only group video decode and x86_64 LTO.

* Sat Nov 14 2026 Zalos Team <dev@zalos.local> - 1.2.1-1
- Mobile history sync: transient download errors (HTTP 400/404/429/5xx, dropped connections) are retried with backoff; the download no longer logs private URLs (thanks @daiphuc).
- Call status shows the real reason (declined, no answer, unreachable, server unreachable, audio device failure); engine crash shows "lost connection".
- Chat stays where you scroll: scroll-bar and wheel scrolling are no longer pulled back to the newest message.
- Message list and sidebar update in place (no full redraw per message); avatars decode off the UI thread; smoother history loading.
- Reconnect at once after network-up or resume from suspend; stalled logins and requests time out instead of hanging.
- Large uploads and downloads stream from disk; downloads are no longer confused by files with the same name.
- My Documents has its own rail button between Messages and Contacts; the Messages button returns to your last chat.
- Chat info panel floats over the chat in narrow windows instead of hiding it.

* Wed Nov 11 2026 Zalos Team <dev@zalos.local> - 1.2.0-1
- C++ call engine (zalos-call, zalos-call-audio) is the only call engine; no Node.js or Python in calls or packages.
- Fix a crash when starting a call; lighter memory use (lazy QML pages and menus, image provider for chat media).
* Tue Nov 10 2026 Zalos Team <dev@zalos.local> - 1.1.1-1
- Native C++ browser session reader removing external Python dependency for login sessions.
- In-chat system notifications and message logs on call start and completion.
- Mention cursor repositioning and unread group mention filtering in Direct tab.
- Call overlay controls vertical centering and UI refinements.

* Mon Nov 09 2026 Zalos Team <dev@zalos.local> - 1.1.0-1
- Group voice and video calls from group chats, with participant state in the call overlay.
- My Documents thread with a built-in media/file/link browser; legacy cloud labels are migrated.
- Kiki Assistant and zBusiness tools destinations (profile, product catalog, auto reply, package).
- Official and system accounts no longer offer call, add-group or share-contact actions.
- Contacts list quick actions for voice call, video call and chat.
- Call engine always uses the bundled C++ call engine; the Settings engine picker was removed.
- Harden mobile history sync with checksum/size verification and a download stall timeout.
- Larger, anti-aliased system tray unread badge; refreshed documentation and screenshots.

* Sun Nov 08 2026 Zalos Team <dev@zalos.local> - 1.0.8-1
- Fix unread badge resurrection across restarts via dual seen dispatch and client-side fallback read points.
- High-res cover/avatar photos and layered chat background blur effect with proper layout clipping.
- Speed up aarch64 packaging on CI runners using precompiled headers, DWARF suppression, and increased concurrency.
- Fix AppImage AppStream packaging warnings and self-extract type2 runtime to protect against 504 gateway timeouts.
- Sync call engine (callee status code 160) and scope session cookies to Zalo domains.

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
- Fix outgoing 1-1 video call camera state, early file_done race, attachment extensions, and mute sync.
- Optimize build speed and binary size across x86_64 and aarch64.
- Repoint all metadata/homepage to cpfriend1721994 and AppStream id to io.github.cpfriend1721994.zalos.

* Sat Oct 31 2026 Zalos Team <dev@zalos.local> - 1.0.0-1
- Initial Qt6 release with GTK/Qt theming, Wayland/X11 and E2EE support.

