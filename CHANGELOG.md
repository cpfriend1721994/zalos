# Changelog

All notable changes to **Zalos** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.4] - 2026-11-06

First published release (supersedes the unpublished 1.0.1–1.0.3 tags). Ships the full 1.0.0 feature set
for x86_64 and aarch64 across AppImage, .deb, RPM and Nix.

### Changed
- Release CI packages without host bind mounts: the source is baked into a throwaway image (`Dockerfile.app`)
  and artifacts are copied out with `docker cp`, so packaging works on runners whose Docker daemon cannot
  see the job checkout (fixes the exit 127 failure).
- Nix aarch64 build disables the sandbox under QEMU emulation (fixes `unable to load seccomp BPF program`).
- Repointed all metadata, homepages, download links and docs to `github.com/cpfriend1721994/zalos`; the
  website is `https://cpfriend1721994.github.io/zalos/`.
- AppStream component id changed to `io.github.cpfriend1721994.zalos` to match the actual GitHub owner.

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
