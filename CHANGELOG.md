# Changelog

All notable changes to **Zalos** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2026-10-31

### Added
- **Core Qt6 C++/QML Architecture**: Complete native client replacing legacy Electron shell, reducing RAM footprint to ~50–90 MB.
- **Wayland & X11 Dual Backend**: Seamless support with `QT_QPA_PLATFORM="wayland;xcb"` and automatic compositor fallback.
- **Client-Side Decorations (CSD)**: Integrated Wayland draggable titlebar with window controls, solving the missing titlebar issue on GNOME Wayland.
- **GTK & Qt Dynamic Theming**: Real-time theme synchronization with GNOME and KDE via XDG Desktop Portal Settings (`org.freedesktop.portal.Settings`).
- **Native E2EE Decryption (`db-cross-v4`)**: Clean-room C++ port of AES-256-CBC, OpenSSL EVP, LZMA decompression, and direct SQLite database querying.
- **System Tray Integration**: D-Bus `StatusNotifierItem` tray icon with dynamically painted unread message count badge overlay.
- **Multi-Format Packaging**:
  - Standalone `.AppImage` with portable `AppRun` environment.
  - Native `.rpm` package with full Fedora 40/41 spec file.
  - Sandboxed `.flatpak` manifest with `org.kde.Platform 6.7`.
- **Unit Testing Suite**: Qt Test framework covering cryptographic key derivation, round-trip decryption, and data models.
- **Docker-First Toolchain**: Complete containerized environment for building, packaging, and headless GUI verification.

