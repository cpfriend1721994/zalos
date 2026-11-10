# Zalos 🚀

<div align="center">

**A high-performance, ultra-lightweight native Zalo desktop client for Linux**

[![CI/CD Pipeline](https://github.com/cpfriend1721994/zalos/actions/workflows/ci.yml/badge.svg)](https://github.com/cpfriend1721994/zalos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Binary Size](https://img.shields.io/badge/Size-~4.8%20MB-success.svg)](#-overview)
[![RAM Usage](https://img.shields.io/badge/RAM-~40--65%20MB-brightgreen.svg)](#-overview)
[![Platform](https://img.shields.io/badge/Platform-Wayland%20%7C%20X11-purple.svg)](#-features)
[![Packaging](https://img.shields.io/badge/Packaging-deb%20%7C%20RPM%20%7C%20AppImage%20%7C%20Nix-orange.svg)](#-installation)

[English](README.md) | [Tiếng Việt](README.vi.md) | [Website](https://cpfriend1721994.github.io/zalos/)

</div>

---

## 📖 Overview

**Zalos** is an open-source, **high-performance and ultra-lightweight** native desktop client for Zalo on Linux.
Written completely in native C++17 and Qt 6 QML, it replaces the resource-heavy Electron/Chromium runtime with a lean,
purpose-built architecture:

- **Ultra-lightweight:** Compact binary package (~4.8 MB AppImage) and minimal memory footprint (~40–65 MB RAM, saving over 90% compared to 450–1,800+ MB on Electron wrappers).
- **High performance:** Instant startup (<0.5s), hardware-accelerated 60 FPS graphics, multi-threaded C++ cryptographic decoding, and smooth responsiveness even on low-spec hardware.
- **Linux native:** Wayland & X11 support with system theme synchronization (light/dark), native desktop notifications, and seamless Vietnamese input.

### ⚡ Performance & Resource Benchmark

| Metric | Zalos (C++17 / Qt 6 QML) | Zalo PC / Electron Wrapper |
|---|---|---|
| **Runtime Memory (RAM)** | **~40 – 65 MB** (self-managed, >90% lighter) | 450 – 1,800+ MB (multiple Chromium processes) |
| **Package / Binary Size** | **~4.8 MB** (compact native build) | > 150 – 250 MB (bundled browser runtime) |
| **Cold Startup Time** | **< 0.5s** (instantaneous launch) | 3 – 8s (V8 engine & browser init) |
| **UI Rendering** | **Hardware-accelerated Qt Quick (60 FPS)** | Web DOM / Chromium compositor |
| **Architecture** | **Pure Native C++17 & QML** | Electron / Node.js + Web wrapper |

---

## ✨ Features

- **High performance & ultra-lightweight** — sub-second startup (<0.5s), smooth 60 FPS Qt Quick rendering, ~4.8 MB download size, and minimal RAM usage (~40–65 MB).
- **Wayland & X11** — runs on both, with native window controls on GNOME and KDE.
- **Automatic light/dark theme** — follows your system colour scheme in real time.
- **Messaging** — one-to-one and group chats, animated stickers, emoji, reactions, replies,
  forwards, pinned messages, labels, and full-history search.
- **Voice & video calls** — 1-1 and group voice/video calls with OpenH264 video encoding.
- **My Documents** — the personal storage thread with instant filters for photos, videos, files and links.
- **Desktop utilities** — screenshot capture (`Ctrl+Alt+S`), Kiki assistant and zBusiness tools (profile, product catalog, auto reply).
- **Contacts quick actions** — 1-click voice call, video call, and direct chat on every contact row.
- **System tray** — unread-count badge and quick actions.
- **Offline history** — your conversations stay available without a connection.
- **Vietnamese interface.**

> **Disclaimer:** Zalos is an independent open-source project and is **not** affiliated with, endorsed, or
> sponsored by VNG Corporation or Zalo. All trademarks and logos are the property of their respective owners.

---

## 📥 Installation

Pick the package for your distribution. Every release is built on the **oldest** release it supports and
installed on the oldest and newest one in CI.

| Distribution | Package | Supported releases |
|---|---|---|
| Ubuntu (and Mint, Pop!_OS, Zorin, elementary…) | `.deb` | Ubuntu **24.04 LTS** and newer |
| Fedora | `.rpm` | Fedora **43** and newer |
| NixOS / any distro with Nix | Nix flake | nixos-unstable |
| openSUSE, Debian, others | AppImage | any recent distribution |

### Ubuntu 24.04+ (`.deb`)
```bash
wget https://github.com/cpfriend1721994/zalos/releases/latest/download/zalos_1.1.1-1~ubuntu24.04_amd64.deb
sudo apt install ./zalos_1.1.1-1~ubuntu24.04_amd64.deb
```

### Fedora 43+ (`.rpm`)
```bash
sudo dnf install https://github.com/cpfriend1721994/zalos/releases/latest/download/zalos-1.1.1-1.fc43.x86_64.rpm
```

### NixOS / Nix
```bash
nix run github:cpfriend1721994/zalos     # run without installing
nix profile install github:cpfriend1721994/zalos
```

### AppImage (other distributions)
```bash
# x86_64
wget https://github.com/cpfriend1721994/zalos/releases/latest/download/Zalos-1.1.1-x86_64.AppImage
chmod +x Zalos-1.1.1-x86_64.AppImage
./Zalos-1.1.1-x86_64.AppImage

# ARM64 (aarch64)
wget https://github.com/cpfriend1721994/zalos/releases/latest/download/Zalos-1.1.1-aarch64.AppImage
chmod +x Zalos-1.1.1-aarch64.AppImage
./Zalos-1.1.1-aarch64.AppImage
```
The AppImage relies on your system's runtime; on a minimal install you may need the usual desktop
runtime packages.

### Arch Linux & other distributions
No prebuilt native package is published for Arch Linux. Build it from source — clone this repository and
follow [Building from source](#️-building-from-source) — or just use the portable **AppImage** above.

> Every release ships **separate x86_64 and aarch64 (ARM64)** files for each format — download the one that
> matches your `uname -m`.

### Optional: calls
Voice and video calls use the open-source call engine from
[zalo-for-linux](https://github.com/VN-Linux-Family/zalo-for-linux) (MIT), bundled in every Zalos package.
The distribution packages pull in what they need automatically. With the AppImage, install the call runtime yourself:

- Ubuntu: `sudo apt install nodejs python3 libopus0 pulseaudio-utils`
- Fedora: `sudo dnf install nodejs python3 opus pulseaudio-utils`

Seeing the other person's camera additionally needs **OpenH264** from your distribution (never bundled, for
patent-licensing reasons): Ubuntu `sudo apt install libopenh264-7`, Fedora `sudo dnf install openh264`.

---

## 🛠️ Building from source

Builds run in containers, so the only prerequisites are `docker` (or `podman`) with Compose and Git.

```bash
git clone https://github.com/cpfriend1721994/zalos.git
cd zalos

docker compose run --rm build            # compile
docker compose run --rm test             # unit tests
docker compose run --rm gui-check        # headless GUI sanity check

docker compose run --rm package-appimage # dist/Zalos-*.AppImage
docker compose run --rm package-deb      # dist/zalos_*_amd64.deb
docker compose run --rm package-rpm      # dist/*.x86_64.rpm
docker compose run --rm package-nix      # dist/*-nixos.tar.gz
```

Pushing a semantic tag (`git tag v1.0.1 && git push origin v1.0.1`) builds the full package set and publishes
a GitHub Release.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the contributor workflow.

---

## 🤝 Contributing

Contributions are welcome. Please read the [Contributing Guidelines](CONTRIBUTING.md) and
[Code of Conduct](CODE_OF_CONDUCT.md) before opening a pull request.

---

## 📄 License

Distributed under the **MIT License**. See [LICENSE](LICENSE).
