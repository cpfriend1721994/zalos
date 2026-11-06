# Changelog

All notable changes to **Zalos** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.4] - 2026-11-06

First published multi-architecture release (supersedes the unpublished `v1.0.1`–`v1.0.3` tags). Ships the full
native Qt6 C++17/QML client for `x86_64` and `aarch64` across AppImage, `.deb`, RPM and Nix, along with call
engine, file upload, attachment naming, mute sync, and build/packaging improvements over `v1.0.3`.

### Fixed (since v1.0.3)
- **1-1 outgoing video calls (`zcall-native` `2b93852`)**: preserved the caller's `camReported` state across
  `_camMic()` and re-sent camera status 2 s after `cmd 408` (`connect`), fixing a bug where the callee's phone
  displayed our camera as off for the entire call even while H.264 video frames were streaming.
- **Fast file & voice upload race (`file_done`)**: buffered early WebSocket `601` `act_type: "file_done"` events
  (`m_earlyFileDone`, 120 s TTL) so small single-chunk files or voice notes whose `file_done` push arrives a few
  milliseconds before the final HTTP `asyncfile/upload` response never miss their waiter or time out.
- **Attachment filenames & extensions (`share.file`)**: appended `params.fileExt` when `content.title` omits the
  file extension and avoided saving files as `"renewMedia"` when served through `/renewMedia` URLs, ensuring file
  type badges and `~/Downloads/Zalos/` desktop opens work reliably.
- **Bidirectional mute state sync (`getmute` & realtime `mute`)**: reconciled `m_mutedIds` against the full
  `/api/social/profile/getmute` snapshot (respecting server `systemTime` and `muteMode` `2`/`3` indefinite mutes)
  so unmuting a conversation on mobile clears the mute indicator on Zalos after reconnect/resync.
- **Nix `aarch64` packaging under QEMU**: disabled the Nix syscall sandbox when `ZALOS_EMULATED=1` (fixes
  `unable to load seccomp BPF program: Invalid argument` on emulated ARM64 builds) and skipped duplicate
  `nix flake update` runs under emulation.

### Performance
- **Faster offline start-up**: the cached conversation list is loaded in a single batched model update
  (one sort/reset instead of one per conversation), and the pre-`QApplication` input-method D-Bus probe issues
  at most one `ListNames` query instead of up to four sequential `dbus-send` calls.
- **Smoother message-list scrolling**: each `MessageItem` caches its local calendar day (`dayCache`), so the
  grouping and date-separator roles no longer repeat timezone-aware `QDateTime` conversions on every row, per frame.
- **Smaller binaries & faster multi-arch builds**: enabled `-fvisibility=hidden`, `-fvisibility-inlines-hidden`,
  `-ffunction-sections -fdata-sections` and `-Wl,--gc-sections`; shared a single `zalos_test_core` `OBJECT` library
  across `test_e2e` and `test_system` (−38 % compile steps); disabled redundant RPM debuginfo/SRPM extraction; and
  reused a single Ubuntu 24.04 Release build for both `.deb` and `AppImage` (−50 % Ubuntu compile time on `x86_64`
  and `aarch64`).

### Changed
- Release CI packages without host bind mounts: the source is baked into a throwaway image (`Dockerfile.app`)
  and artifacts are copied out with `docker cp`, so packaging works on runners whose Docker daemon cannot
  see the job checkout (fixes the exit 127 failure in `v1.0.3`).
- Redesigned the project documentation site (`https://cpfriend1721994.github.io/zalos/`) with native Light/Dark
  themes, real application screenshots, architecture deep-dive, and per-distro installation tabs.
- Repointed all metadata, homepages, download links and docs to `github.com/cpfriend1721994/zalos` and updated the
  AppStream component id to `io.github.cpfriend1721994.zalos`.

## [1.0.0] - 2026-10-31

### Added
- Native Zalo desktop client for Linux — light on memory and quick to start.
- Wayland and X11 support, with native window controls (including on GNOME Wayland).
- Automatic light/dark theme that follows the system colour scheme.
- Messaging: one-to-one and group chats, animated stickers, emoji, reactions, replies, forwards,
  pinned messages, labels, and full-history search.
- Voice and video calls.
- System tray icon with an unread-count badge.
- Offline access to your conversation history.
- Vietnamese interface.
- Packaging for `.deb`, `.rpm`, Arch, AppImage and Nix.
