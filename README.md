# Zalos 🚀

<div align="center">

**A fast, native Zalo desktop client for Linux**

[![CI/CD Pipeline](https://github.com/zalos/zalos/actions/workflows/ci.yml/badge.svg)](https://github.com/zalos/zalos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Wayland%20%7C%20X11-purple.svg)](#features)
[![Packaging](https://img.shields.io/badge/Packaging-deb%20%7C%20RPM%20%7C%20AppImage%20%7C%20Nix-orange.svg)](#installation)

[English](README.md) | [Tiếng Việt](README.vi.md) | [Website](https://zalos.github.io)

</div>

---

## 📖 Overview

**Zalos** is an open-source, native desktop client for Zalo on Linux. It is light on memory, starts quickly,
and feels at home on modern Linux desktops — with proper window controls, system theme following, and a
Vietnamese interface.

It is built with Qt 6 (C++ & QML) and distributed for the common Linux package formats.

---

## ✨ Features

- **Fast and lightweight** — quick startup and a small memory footprint.
- **Wayland & X11** — runs on both, with native window controls on GNOME and KDE.
- **Automatic light/dark theme** — follows your system colour scheme in real time.
- **Messaging** — one-to-one and group chats, animated stickers, emoji, reactions, replies,
  forwards, pinned messages, labels, and full-history search.
- **Voice & video calls.**
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
wget https://github.com/zalos/zalos/releases/latest/download/zalos_1.0.0-1~ubuntu24.04_amd64.deb
sudo apt install ./zalos_1.0.0-1~ubuntu24.04_amd64.deb
```

### Fedora 43+ (`.rpm`)
```bash
sudo dnf install https://github.com/zalos/zalos/releases/latest/download/zalos-1.0.0-1.fc43.x86_64.rpm
```

### NixOS / Nix
```bash
nix run github:zalos/zalos            # run without installing
nix profile install github:zalos/zalos
```

### AppImage (other distributions)
```bash
wget https://github.com/zalos/zalos/releases/latest/download/Zalos-1.0.0-x86_64.AppImage
chmod +x Zalos-1.0.0-x86_64.AppImage
./Zalos-1.0.0-x86_64.AppImage
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
The distribution packages pull in what it needs automatically. With the AppImage, install the extras yourself:

- Ubuntu: `sudo apt install nodejs python3 libopus0 pulseaudio-utils`
- Fedora: `sudo dnf install nodejs python3 opus pulseaudio-utils`

Seeing the other person's camera additionally needs **OpenH264** from your distribution (never bundled, for
patent-licensing reasons): Ubuntu `sudo apt install libopenh264-7`, Fedora `sudo dnf install openh264`.

---

## 🛠️ Building from source

Builds run in containers, so the only prerequisites are `docker` (or `podman`) with Compose and Git.

```bash
git clone https://github.com/zalos/zalos.git
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
