# Zalos 🚀

<div align="center">

**A High-Performance, Native Linux Client for Zalo built with Qt6 (C++ & QML)**

[![CI/CD Pipeline](https://github.com/zalos/zalos/actions/workflows/ci.yml/badge.svg)](https://github.com/zalos/zalos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Qt Version](https://img.shields.io/badge/Qt-6.8%2B-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/Platform-Wayland%20%7C%20X11-purple.svg)](#features)
[![RAM Usage](https://img.shields.io/badge/RAM-~50MB-success.svg)](#benchmarks)
[![Packaging](https://img.shields.io/badge/Packaging-deb%20%7C%20RPM%20%7C%20AppImage%20%7C%20NixOS-orange.svg)](#installation)

[English](README.md) | [Tiếng Việt](README.vi.md) | [Official Website / Landing Page](https://zalos.github.io)

</div>

---

## 📖 Overview

**Zalos** is an open-source, native desktop client for Zalo on Linux, engineered from scratch using **Qt6 (C++ and QML)**.

Inspired by and ported from the clean-room reverse-engineering efforts of [`zalo-for-linux`](../zalo-for-linux), **Zalos** eliminates the bloated Electron runtime in favor of a lean, native application. It delivers instantaneous startup times, an ultra-low memory footprint (~50MB RAM), seamless **Wayland Client-Side Decorations (CSD)**, dynamic **GTK / Qt theme adaptation**, and pure C++ **End-to-End Encryption (E2EE)** message decryption.

---

## ⚡ Benchmarks: Zalos vs. Legacy Electron

| Metric | Zalos (Qt6 Native) | Legacy Zalo (Electron Wrapper) |
| :--- | :---: | :---: |
| **Idle Memory (RAM)** | **~50 MB – 85 MB** | ~750 MB – 1.4 GB |
| **Cold Startup Time** | **< 0.25 seconds** | ~2.5 – 5.0 seconds |
| **Display Server** | **Native Wayland (`qtwayland`) & X11** | Chromium Ozone (often crashes or loses window frame) |
| **Window Frame on GNOME** | **Native CSD with window drag & controls** | Broken / missing minimize & maximize buttons |
| **Theme Sync** | **Real-time via D-Bus Portal & GNOME Settings** | Requires manual app reload |
| **Cryptographic Decryptor** | **Native C++ (`db-cross-v4` via OpenSSL EVP)** | Node-API `.node` binding with Wine fallback |

---

## ✨ Key Features

- **🚀 Native Qt Quick & QML Scene Graph**:
  Hardware-accelerated rendering utilizing Vulkan, OpenGL, or Software Rasterizer (Mesa llvmpipe).
- **🖥️ Dual Wayland & Xorg Support**:
  Configured with `QT_QPA_PLATFORM="wayland;xcb"`. Automatically detects the active display server with zero compositor crashes.
- **🎨 Adaptive GTK & Qt Theming**:
  Dynamically syncs with system Dark and Light color schemes via Freedesktop XDG Desktop Portal Settings (`org.freedesktop.portal.Settings`) and GNOME interfaces (`org.gnome.desktop.interface`).
- **🔒 Pure C++ E2EE Synchronization (`db-cross-v4`)**:
  Standalone C++ implementation of AES-256-CBC with PKCS#7 unpadding, OpenSSL EVP, and LZMA decompression, enabling offline encrypted SQLite message sync without Node.js.
- **🔔 System Tray with Dynamic Unread Badge**:
  FreeDesktop `StatusNotifierItem` tray controller that draws unread message counts directly onto the application icon.
- **📞 Audio & Video Calling Architecture**:
  Engineered to communicate with `zcall-native` (ZRTP/SRTP signaling) using PipeWire and PulseAudio pipelines.
- **📦 Universal Linux Packaging**:
  Automated containerized builds for **.deb** (Ubuntu 24.04+), **RPM** (Fedora 43+), **AppImage** and **Nix**.

---

## 📥 Installation

Pick the package for your distribution. Every release is built on the **oldest** release it supports and
installed on the oldest and newest one in CI.

| Distribution | Package | Supported releases |
|---|---|---|
| Ubuntu (and derivatives: Mint, Pop!_OS, Zorin, elementary…) | `.deb` | Ubuntu **24.04 LTS** and newer (tested: 24.04, 26.04, 26.10) |
| Fedora | `.rpm` | Fedora **43** and newer (tested: 43, 45) |
| Arch Linux (and Manjaro, EndeavourOS…) | `PKGBUILD` (build from source) or prebuilt `.pkg.tar.zst` | rolling (tested on the current `archlinux` image) |
| NixOS / any distro with Nix | Nix flake | nixos-unstable |
| openSUSE, Debian, others | AppImage | any distro with **Qt ≥ 6.4** and **glibc ≥ 2.39** installed |

### Ubuntu 24.04+ (`.deb`)
```bash
wget https://github.com/zalos/zalos/releases/latest/download/zalos_1.0.0-1~ubuntu24.04_amd64.deb
sudo apt install ./zalos_1.0.0-1~ubuntu24.04_amd64.deb
```
`apt` pulls in Qt 6 and its QML modules from the Ubuntu archive. Debian 13 should work with the same file but is
not tested yet.

### Fedora 43+ (`.rpm`)
```bash
sudo dnf install https://github.com/zalos/zalos/releases/latest/download/zalos-1.0.0-1.fc43.x86_64.rpm
```
On Fedora 45+ `dnf` also installs `openssl3-libs` (Fedora's OpenSSL 3 compatibility library).

### Arch Linux (build from source with `makepkg`)
```bash
mkdir zalos-pkg && cd zalos-pkg
wget https://github.com/zalos/zalos/releases/latest/download/PKGBUILD
makepkg -si          # downloads the tagged source, builds against your Qt 6 and installs it with pacman
```
Or install the prebuilt package from the same release:
`sudo pacman -U zalos-1.0.0-1-x86_64.pkg.tar.zst`. Optional extras: `sudo pacman -S openh264 libpulse`
(call video and calls).

### NixOS / Nix
```bash
nix run github:zalos/zalos            # run without installing
nix profile install github:zalos/zalos
```
In `configuration.nix`: `environment.systemPackages = [ (pkgs.callPackage ./path-to-zalos {}) ];`

### AppImage (other distributions)
The AppImage contains Zalos only; **Qt 6 (≥ 6.4) and its QML modules come from your system**:
```bash
# Fedora (if you prefer the AppImage over the RPM)
sudo dnf install qt6-qtdeclarative qt6-qtmultimedia qt6-qtsvg qt6-qtwebsockets qt6-qtwayland

wget https://github.com/zalos/zalos/releases/latest/download/Zalos-1.0.0-x86_64.AppImage
chmod +x Zalos-1.0.0-x86_64.AppImage
./Zalos-1.0.0-x86_64.AppImage
```

### Optional: calls and call video
- **Voice/video calls** use the zcall-native engine of
  [zalo-for-linux](https://github.com/VN-Linux-Family/zalo-for-linux) (MIT), which ships inside every Zalos package
  (`share/zalos/zcall-native`). It runs on the system's `node` (Node.js, or Bun's `node`), `python3` and libopus with
  the PulseAudio/PipeWire tools: the deb/RPM/Arch/Nix packages pull these in; with the AppImage install them yourself
  (`sudo apt install nodejs python3 libopus0 pulseaudio-utils`, `sudo dnf install nodejs python3 opus pulseaudio-utils`).
- **Seeing the other person's camera** needs **OpenH264** from your distribution (it is never bundled, for patent
  licensing reasons): Ubuntu `sudo apt install libopenh264-7` (24.04) / `libopenh264-8` (newer), Fedora
  `sudo dnf install openh264` (from the `fedora-cisco-openh264` repository, enabled by default), Nix: included.

---

## 🏛️ System Architecture

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        Zalos Application Process                       │
│                                                                        │
│  ┌───────────────────────┐          ┌───────────────────────────────┐  │
│  │   QML User Interface  │◀──Signal─│      Qt6 C++ Backend Core     │  │
│  │ (Sidebar, Chat, CSD)  │───Slot──▶│ (ZaloClient, Models, Theming) │  │
│  └───────────────────────┘          └───────────────┬───────────────┘  │
│                                                     │                  │
└─────────────────────────────────────────────────────┼──────────────────┘
                                                      │
                       ┌──────────────────────────────┼──────────────────────────────┐
                       ▼                              ▼                              ▼
             ┌───────────────────┐          ┌───────────────────┐          ┌───────────────────┐
             │    OpenSSL/LZMA   │          │  XDG Desktop      │          │   Wayland / X11   │
             │   db-cross-v4     │          │  Portal (GTK/D-Bus│          │   QPA Subsystem   │
             │   E2EE Decryption │          │  Theming & Tray)  │          │   Compositor      │
             └───────────────────┘          └───────────────────┘          └───────────────────┘
```

---

## 🐳 Building from Source (Docker-First Workflow)

In accordance with strict project engineering guidelines, **developers and CI/CD must never compile or test directly on the host machine**. All builds are strictly containerized:

```bash
# 1. Clone repository
git clone https://github.com/zalos/zalos.git
cd zalos

# 2. Build the Docker environment & compile release binaries
docker compose run --rm build

# 3. Execute unit test suite (CTest)
docker compose run --rm test

# 4. Perform headless GUI verification (Xvfb)
docker compose run --rm gui-check

# 5. Build Distribution Packages
docker compose run --rm package-appimage   # Generates dist/Zalos-*.AppImage (Ubuntu 24.04 base)
docker compose run --rm package-deb        # Generates dist/zalos_*~ubuntu24.04_amd64.deb
docker compose run --rm package-rpm        # Generates dist/*.fc43.x86_64.rpm
docker compose run --rm package-arch       # Generates dist/zalos-*-x86_64.pkg.tar.zst (makepkg)
docker compose run --rm package-nix        # Generates dist/*-nixos.tar.gz
```

---

## 🧪 Local Container Testing Runbook

Developers can verify every packaging format locally without host dependencies using dedicated container test suites:

### 1. Fedora RPM Testing
- **Automated clean DNF install & headless check in Fedora 43 (and `test-fedora-rpm-latest` for Fedora 45)**:
  ```bash
  docker compose run --rm test-fedora-rpm
  ```
- **Interactive Qt6 GUI on host screen**:
  ```bash
  xhost +local:
  docker compose run --rm test-fedora-gui
  ```
- **Interactive shell inside Fedora container**:
  ```bash
  docker compose run --rm test-fedora-rpm bash
  ```

### 2. AppImage Testing
- **Automated extraction, library validation & headless check**:
  ```bash
  docker compose run --rm test-appimage
  ```
- **Interactive AppImage GUI on host screen**:
  ```bash
  xhost +local:
  docker compose run --rm test-appimage-gui
  ```

### 3. NixOS / Nix Flake Testing
- **Automated flake schema validation, build & CLI check**:
  ```bash
  docker compose run --rm test-nix
  ```
- **Interactive Nix development shell**:
  ```bash
  docker compose run --rm test-nix-shell
  ```

---

## 🚀 Release Process

- **Standard Commits**: Pushing code to `main` triggers rapid containerized unit tests and headless GUI checks. Packaging steps are bypassed for instant CI feedback.
- **Publishing Releases**: To trigger the full packaging suite (RPM, AppImage, NixOS) and publish a GitHub Release with attached binaries, push a semantic tag:
  ```bash
  git tag v1.0.1
  git push origin v1.0.1
  ```

---

## 🧪 Testing Coverage

The automated test suite runs in Docker and validates:
- **`test_crypto`**: AES-256-CBC encryption/decryption round trips, uppercase key derivation, and empty payload safety.
- **`test_models`**: `ConversationModel` and `MessageModel` list operations, row insertion notifications, and unread counter synchronization.
- **`gui-check`**: Headless Xvfb virtual display verification ensuring QML modules, assets, and shaders resolve without crashes.

---

## 🤝 Contributing

We welcome community contributions! Please read our [Contributing Guidelines](CONTRIBUTING.md) and [Code of Conduct](CODE_OF_CONDUCT.md) before submitting pull requests.

---

## 📄 License & Disclaimer

- Distributed under the **MIT License**. See [LICENSE](LICENSE) for details.
- **Disclaimer**: Zalos is an independent open-source project and is **not** affiliated with, endorsed, or sponsored by VNG Corporation or Zalo. All trademarks and logos are the property of their respective owners.
