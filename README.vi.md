# Zalos 🚀

<div align="center">

**Ứng dụng Zalo Desktop Native cho Linux phát triển bằng Qt6 (C++ & QML)**

[![CI/CD Pipeline](https://github.com/zalos/zalos/actions/workflows/ci.yml/badge.svg)](https://github.com/zalos/zalos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Phiên bản Qt](https://img.shields.io/badge/Qt-6.8%2B-green.svg)](https://www.qt.io/)
[![Hỗ trợ nền tảng](https://img.shields.io/badge/Platform-Wayland%20%7C%20X11-purple.svg)](#tính-năng-nổi-bật)
[![Mức tiêu thụ RAM](https://img.shields.io/badge/RAM-~50MB-success.svg)](#so-sánh-hiệu-năng)
[![Đóng gói](https://img.shields.io/badge/Đóng%20gói-deb%20%7C%20RPM%20%7C%20AppImage%20%7C%20NixOS-orange.svg)](#hướng-dẫn-cài-đặt)

[English](README.md) | [Tiếng Việt](README.vi.md) | [Trang chủ / Landing Page](https://zalos.github.io)

</div>

---

## 📖 Giới thiệu Tổng quan

**Zalos** là ứng dụng chat Zalo Desktop mã nguồn mở dành riêng cho hệ điều hành Linux, được xây dựng hoàn toàn từ đầu bằng **Qt6 (C++ và QML)**.

Dự án được truyền cảm hứng và kế thừa thành quả reverse-engineering từ [`zalo-for-linux`](../zalo-for-linux), thay thế lớp vỏ bọc Electron cồng kềnh bằng ứng dụng Native gọn nhẹ. **Zalos** mang lại tốc độ khởi động tức thì, mức tiêu thụ RAM cực thấp (~50MB), hỗ trợ mượt mà **Wayland Client-Side Decorations (CSD)**, tự động hòa nhập **Theme GTK (GNOME) lẫn Qt (KDE)**, và tích hợp thuật toán giải mã tin nhắn mã hóa đầu cuối **E2EE (`db-cross-v4`)** viết hoàn toàn bằng C++.

---

## ⚡ So sánh Hiệu năng: Zalos vs. Zalo Electron cũ

| Tiêu chí | Zalos (Qt6 Native) | Zalo Desktop cũ (Electron Shell) |
| :--- | :---: | :---: |
| **Mức tiêu thụ RAM (Idle)** | **~50 MB – 85 MB** | ~750 MB – 1.4 GB |
| **Thời gian khởi động lạnh**| **< 0.25 giây** | ~2.5 – 5.0 giây |
| **Display Server** | **Native Wayland (`qtwayland`) & X11** | Chromium Ozone (dễ crash hoặc mất viền cửa sổ) |
| **Thanh tiêu đề trên GNOME**| **Native CSD kéo thả mượt mà** | Mất nút thu nhỏ/phóng to trên Wayland |
| **Đồng bộ Theme Sáng/Tối** | **Tự động theo D-Bus Portal & GNOME** | Phải tải lại app thủ công |
| **Mô-đun giải mã E2EE** | **C++ Native (`db-cross-v4` qua OpenSSL)**| Addon Node.js `.node` phụ thuộc Wine |

---

## ✨ Tính Năng Nổi Bật

- **🚀 Giao diện Qt Quick & QML Hiện Đại**: Render trực tiếp bằng GPU qua Vulkan/OpenGL, đảm bảo độ mượt 60/120 FPS.
- **🖥️ Hỗ trợ Song Song Wayland & Xorg**: Cấu hình `QT_QPA_PLATFORM="wayland;xcb"`, tự động nhận diện môi trường hiển thị.
- **🎨 Hòa nhập Giao diện GTK & Qt**: Tự động đổi màu Dark/Light theo hệ điều hành qua `org.freedesktop.portal.Settings` và giao diện GNOME.
- **🔒 Giải mã Tin nhắn E2EE bằng C++ (`db-cross-v4`)**: Port C++ độc lập dùng OpenSSL EVP AES-256-CBC, PKCS#7 unpadding và LZMA để đọc cơ sở dữ liệu SQLite offline an toàn mà không cần Node.js.
- **🔔 Khay Hệ Thống với Badge Đếm**: Vẽ trực tiếp số tin nhắn chưa đọc lên icon tray thông qua giao thức D-Bus `StatusNotifierItem`.
- **📞 Kiến trúc Cuộc gọi & Âm thanh**: Sẵn sàng giao tiếp với engine `zcall-native` (ZRTP/SRTP) qua PipeWire và PulseAudio.
- **📦 Đa dạng Định dạng Cài đặt**: Tự động đóng gói **.deb** (Ubuntu 24.04+), **RPM** (Fedora 43+), **AppImage** và **Nix**.

---

## 📥 Hướng Dẫn Cài Đặt

Chọn gói theo bản phân phối bạn đang dùng. Mỗi bản phát hành được build trên phiên bản **cũ nhất** mà nó hỗ trợ,
và được CI cài thử trên cả bản cũ nhất lẫn bản mới nhất.

| Bản phân phối | Gói | Phiên bản hỗ trợ |
|---|---|---|
| Ubuntu (và các bản dựa trên Ubuntu: Mint, Pop!_OS, Zorin, elementary…) | `.deb` | Ubuntu **24.04 LTS** trở lên (đã kiểm thử: 24.04, 26.04, 26.10) |
| Fedora | `.rpm` | Fedora **43** trở lên (đã kiểm thử: 43, 45) |
| Arch Linux (và Manjaro, EndeavourOS…) | `PKGBUILD` (build từ mã nguồn) hoặc gói dựng sẵn `.pkg.tar.zst` | rolling (đã kiểm thử trên image `archlinux` hiện tại) |
| NixOS / mọi bản có Nix | Nix flake | nixos-unstable |
| openSUSE, Debian, các bản khác | AppImage | bản nào có **Qt ≥ 6.4** và **glibc ≥ 2.39** |

### Ubuntu 24.04 trở lên (`.deb`)
```bash
wget https://github.com/zalos/zalos/releases/latest/download/zalos_1.0.0-1~ubuntu24.04_amd64.deb
sudo apt install ./zalos_1.0.0-1~ubuntu24.04_amd64.deb
```
`apt` tự cài Qt 6 và các module QML từ kho của Ubuntu. Debian 13 có thể dùng cùng file này nhưng chưa được kiểm thử.

### Fedora 43 trở lên (`.rpm`)
```bash
sudo dnf install https://github.com/zalos/zalos/releases/latest/download/zalos-1.0.0-1.fc43.x86_64.rpm
```
Trên Fedora 45 trở lên, `dnf` cài kèm `openssl3-libs` (thư viện OpenSSL 3 tương thích của Fedora).

### Arch Linux (build từ mã nguồn bằng `makepkg`)
```bash
mkdir zalos-pkg && cd zalos-pkg
wget https://github.com/zalos/zalos/releases/latest/download/PKGBUILD
makepkg -si          # tải mã nguồn theo tag, build với Qt 6 của máy và cài bằng pacman
```
Hoặc cài gói dựng sẵn trong cùng bản phát hành:
`sudo pacman -U zalos-1.0.0-1-x86_64.pkg.tar.zst`. Gói tùy chọn: `sudo pacman -S openh264 libpulse`
(hình ảnh cuộc gọi và gọi thoại).

### NixOS / Nix
```bash
nix run github:zalos/zalos            # chạy thử, không cài
nix profile install github:zalos/zalos
```
Trong `configuration.nix`: `environment.systemPackages = [ (pkgs.callPackage ./path-to-zalos {}) ];`

### AppImage (các bản phân phối khác)
AppImage chỉ chứa Zalos; **Qt 6 (≥ 6.4) và các module QML lấy từ hệ thống của bạn**:
```bash
# Fedora (nếu bạn muốn dùng AppImage thay cho RPM)
sudo dnf install qt6-qtdeclarative qt6-qtmultimedia qt6-qtsvg qt6-qtwebsockets qt6-qtwayland

wget https://github.com/zalos/zalos/releases/latest/download/Zalos-1.0.0-x86_64.AppImage
chmod +x Zalos-1.0.0-x86_64.AppImage
./Zalos-1.0.0-x86_64.AppImage
```

### Tùy chọn: gọi thoại và hình ảnh cuộc gọi
- **Gọi thoại/video** dùng engine zcall-native của [zalo-for-linux](https://github.com/VN-Linux-Family/zalo-for-linux)
  (MIT), đã đóng gói sẵn trong mọi gói Zalos (`share/zalos/zcall-native`). Engine chạy bằng `node` (Node.js hoặc `node`
  của Bun), `python3`, libopus và bộ công cụ PulseAudio/PipeWire của máy: gói deb/RPM/Arch/Nix tự cài kèm; dùng
  AppImage thì tự cài (`sudo apt install nodejs python3 libopus0 pulseaudio-utils`,
  `sudo dnf install nodejs python3 opus pulseaudio-utils`).
- **Xem camera của người kia** cần **OpenH264** của bản phân phối (Zalos không đóng gói sẵn vì lý do bản quyền sáng
  chế): Ubuntu `sudo apt install libopenh264-7` (24.04) / `libopenh264-8` (bản mới hơn), Fedora
  `sudo dnf install openh264` (kho `fedora-cisco-openh264`, mặc định đã bật), Nix: có sẵn.

---

## 🏛️ Sơ Đồ Kiến Trúc Hệ Thống

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

## 🐳 Tự Biên Dịch từ Mã Nguồn (Quy chuẩn Docker-First)

Theo quy định nghiêm ngặt của dự án, **lập trình viên không biên dịch hoặc chạy test trực tiếp trên máy host**. Toàn bộ quá trình phải diễn ra bên trong container Docker:

```bash
# 1. Clone repository
git clone https://github.com/zalos/zalos.git
cd zalos

# 2. Build môi trường container & biên dịch binary Release
docker compose run --rm build

# 3. Chạy toàn bộ unit test (CTest)
docker compose run --rm test

# 4. Kiểm thử giao diện không đầu (Xvfb)
docker compose run --rm gui-check

# 5. Đóng gói các bộ cài đặt
docker compose run --rm package-appimage   # Sinh file dist/Zalos-*.AppImage (nền Ubuntu 24.04)
docker compose run --rm package-deb        # Sinh file dist/zalos_*~ubuntu24.04_amd64.deb
docker compose run --rm package-rpm        # Sinh file dist/*.fc43.x86_64.rpm
docker compose run --rm package-arch       # Sinh file dist/zalos-*-x86_64.pkg.tar.zst (makepkg)
docker compose run --rm package-nix        # Sinh file dist/*-nixos.tar.gz
```

---

## 🧪 Hướng Dẫn Tự Kiểm Thử Tại Local (Local Container Testing)

Nhằm tuân thủ nguyên tắc **Zero Host Execution** (không cài cắm dependencies hay chạy trực tiếp trên máy chủ), dự án cung cấp sẵn các container test chuyên biệt để bạn tự kiểm thử toàn diện từng định dạng đóng gói ngay tại máy tính:

### 1. Kiểm thử định dạng Fedora RPM
- **Tự động cài đặt dnf & chạy kiểm thử trong Fedora 43 sạch** (`test-fedora-rpm-latest` cho Fedora 45):
  ```bash
  podman compose run --rm test-fedora-rpm
  ```
  *(Container Fedora sẽ nạp file `.rpm` từ `dist/`, chạy `dnf install`, kiểm tra toàn bộ thư viện liên kết động `ldd`, desktop entry, scalable icon và thực hiện headless sanity check).*
- **Chạy trực tiếp giao diện GUI native lên màn hình host**:
  ```bash
  xhost +local:
  podman compose run --rm test-fedora-gui
  ```
- **Vào shell Fedora để thao tác thủ công**:
  ```bash
  podman compose run --rm test-fedora-rpm bash
  ```

### 2. Kiểm thử định dạng AppImage
- **Tự động trích xuất AppDir, kiểm tra thư viện liên kết & headless test**:
  ```bash
  podman compose run --rm test-appimage
  ```
  *(Giải nén `squashfs-root`, kiểm tra `AppRun`, quyền thực thi, `ldd` và chạy test giao diện ảo qua `Xvfb`).*
- **Chạy trực tiếp AppImage hiển thị lên màn hình desktop**:
  ```bash
  xhost +local:
  podman compose run --rm test-appimage-gui
  ```

### 3. Kiểm thử định dạng NixOS / Nix Flakes
- **Tự động kiểm tra cú pháp Flake, build package & kiểm tra CLI**:
  ```bash
  podman compose run --rm test-nix
  ```
  *(Container `nixos/nix` sẽ chạy `nix flake metadata`, `nix build .#default`, kiểm tra file thực thi trong `/nix/store` và script bọc môi trường `wrapQtAppsHook`).*
- **Mở interactive Nix development shell**:
  ```bash
  podman compose run --rm test-nix-shell
  ```

---

## 🚀 Quy Trình Phát Hành (Release)

- **Đẩy code thông thường**: Khi `push` code lên nhánh `main`, hệ thống CI chỉ thực hiện kiểm thử nhanh (biên dịch, chạy CTest và kiểm thử GUI Xvfb) để đảm bảo chất lượng mà không làm mất thời gian đóng gói.
- **Phát hành bản Release**: Để kích hoạt toàn bộ quy trình đóng gói đa định dạng (RPM, AppImage, NixOS) và tự động tạo GitHub Release đính kèm file cài đặt, bạn chỉ cần tạo và đẩy tag phiên bản:
  ```bash
  git tag v1.0.1
  git push origin v1.0.1
  ```

---

## 🤝 Đóng Góp Phát Triển

Chúng tôi rất hoan nghênh các đóng góp từ cộng đồng! Vui lòng đọc kỹ [Hướng dẫn Đóng góp (CONTRIBUTING.md)](CONTRIBUTING.md) và [Quy tắc Ứng xử (CODE_OF_CONDUCT.md)](CODE_OF_CONDUCT.md) trước khi gửi Pull Request.

---

## 📄 Bản Quyền & Miễn Trừ Trách Nhiệm

- Phát hành theo giấy phép **MIT License**. Xem chi tiết tại [LICENSE](LICENSE).
- **Miễn trừ trách nhiệm**: Zalos là dự án độc lập của cộng đồng mã nguồn mở, **không** trực thuộc, không được tài trợ hay đại diện bởi VNG Corporation hoặc Zalo. Tất cả nhãn hiệu và logo thuộc quyền sở hữu của các chủ sở hữu tương ứng.

