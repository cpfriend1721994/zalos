# Zalos 🚀

<div align="center">

**Ứng dụng Zalo cho Linux — nhẹ và nhanh**

[![CI/CD Pipeline](https://github.com/cpfriend1721994/zalos/actions/workflows/ci.yml/badge.svg)](https://github.com/cpfriend1721994/zalos/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Wayland%20%7C%20X11-purple.svg)](#-tính-năng)
[![Packaging](https://img.shields.io/badge/Packaging-deb%20%7C%20RPM%20%7C%20AppImage%20%7C%20Nix-orange.svg)](#-cài-đặt)

[English](README.md) | [Tiếng Việt](README.vi.md) | [Trang chủ](https://cpfriend1721994.github.io/zalos/)

</div>

---

## 📖 Giới thiệu

**Zalos** là ứng dụng Zalo cho Linux, mã nguồn mở. Ứng dụng nhẹ bộ nhớ, khởi động nhanh và hoạt động tự nhiên
trên các môi trường desktop Linux hiện đại — có nút điều khiển cửa sổ đầy đủ, tự đồng bộ giao diện sáng/tối của
hệ thống, và giao diện tiếng Việt.

Ứng dụng được phát triển bằng Qt 6 (C++ & QML) và phát hành cho các định dạng gói phổ biến trên Linux.

---

## ✨ Tính năng

- **Nhẹ và nhanh** — khởi động nhanh, dùng ít bộ nhớ.
- **Wayland & X11** — chạy trên cả hai, có nút điều khiển cửa sổ chuẩn trên GNOME và KDE.
- **Tự đổi giao diện sáng/tối** — bám theo màu hệ thống theo thời gian thực.
- **Nhắn tin** — chat đơn và nhóm, nhãn dán động, emoji, cảm xúc, trả lời, chuyển tiếp,
  ghim tin nhắn, nhãn phân loại, và tìm kiếm toàn bộ lịch sử.
- **Gọi thoại và gọi video.**
- **Khay hệ thống** — hiển thị số tin chưa đọc và thao tác nhanh.
- **Lịch sử ngoại tuyến** — xem lại hội thoại cả khi không có kết nối.
- **Giao diện tiếng Việt.**

> **Miễn trừ trách nhiệm:** Zalos là một dự án mã nguồn mở độc lập, **không** liên kết, không được chứng thực hay
> tài trợ bởi Công ty VNG hoặc Zalo. Mọi thương hiệu và logo thuộc về chủ sở hữu tương ứng.

---

## 📥 Cài đặt

Chọn gói theo bản phân phối của bạn. Mỗi bản phát hành được build trên bản phân phối **cũ nhất** mà nó hỗ trợ,
và được kiểm tra cài đặt trên cả bản cũ nhất lẫn mới nhất trong CI.

| Bản phân phối | Gói | Phiên bản hỗ trợ |
|---|---|---|
| Ubuntu (và Mint, Pop!_OS, Zorin, elementary…) | `.deb` | Ubuntu **24.04 LTS** trở lên |
| Fedora | `.rpm` | Fedora **43** trở lên |
| NixOS / mọi distro có Nix | Nix flake | nixos-unstable |
| openSUSE, Debian, khác | AppImage | mọi bản phân phối gần đây |

### Ubuntu 24.04+ (`.deb`)
```bash
wget https://github.com/cpfriend1721994/zalos/releases/latest/download/zalos_1.0.7-1~ubuntu24.04_amd64.deb
sudo apt install ./zalos_1.0.7-1~ubuntu24.04_amd64.deb
```

### Fedora 43+ (`.rpm`)
```bash
sudo dnf install https://github.com/cpfriend1721994/zalos/releases/latest/download/zalos-1.0.7-1.fc43.x86_64.rpm
```

### NixOS / Nix
```bash
nix run github:cpfriend1721994/zalos     # chạy thử không cài
nix profile install github:cpfriend1721994/zalos
```

### AppImage (các bản phân phối khác)
```bash
# x86_64
wget https://github.com/cpfriend1721994/zalos/releases/latest/download/Zalos-1.0.7-x86_64.AppImage
chmod +x Zalos-1.0.7-x86_64.AppImage
./Zalos-1.0.7-x86_64.AppImage

# ARM64 (aarch64)
wget https://github.com/cpfriend1721994/zalos/releases/latest/download/Zalos-1.0.7-aarch64.AppImage
chmod +x Zalos-1.0.7-aarch64.AppImage
./Zalos-1.0.7-aarch64.AppImage
```
AppImage dùng môi trường runtime sẵn có của hệ thống; trên bản cài tối giản bạn có thể cần cài thêm các gói
runtime desktop thông dụng.

### Arch Linux & các bản phân phối khác
Dự án không phát hành gói dựng sẵn cho Arch Linux. Hãy **tự build từ mã nguồn** — clone kho này và làm theo
[Build từ mã nguồn](#️-build-từ-mã-nguồn) — hoặc dùng **AppImage** di động ở trên.

> Mỗi bản phát hành có **file riêng cho x86_64 và aarch64 (ARM64)** ở từng định dạng — tải đúng theo `uname -m`.

### Tuỳ chọn: cuộc gọi
Gọi thoại và video dùng engine gọi mã nguồn mở từ
[zalo-for-linux](https://github.com/VN-Linux-Family/zalo-for-linux) (MIT), được đóng gói sẵn trong mọi gói Zalos.
Các gói phân phối sẽ tự kéo về những thứ cần thiết. Với AppImage, hãy cài thêm:

- Ubuntu: `sudo apt install nodejs python3 libopus0 pulseaudio-utils`
- Fedora: `sudo dnf install nodejs python3 opus pulseaudio-utils`

Để thấy camera của người bên kia còn cần **OpenH264** từ bản phân phối của bạn (không bao giờ đóng gói kèm, vì lý
do bản quyền sáng chế): Ubuntu `sudo apt install libopenh264-7`, Fedora `sudo dnf install openh264`.

---

## 🛠️ Build từ mã nguồn

Quá trình build chạy trong container, nên chỉ cần `docker` (hoặc `podman`) kèm Compose và Git.

```bash
git clone https://github.com/cpfriend1721994/zalos.git
cd zalos

docker compose run --rm build            # biên dịch
docker compose run --rm test             # unit test
docker compose run --rm gui-check        # kiểm tra GUI headless

docker compose run --rm package-appimage # dist/Zalos-*.AppImage
docker compose run --rm package-deb      # dist/zalos_*_amd64.deb
docker compose run --rm package-rpm      # dist/*.x86_64.rpm
docker compose run --rm package-nix      # dist/*-nixos.tar.gz
```

Đẩy một tag theo chuẩn semantic (`git tag v1.0.1 && git push origin v1.0.1`) sẽ build toàn bộ gói và phát hành
một GitHub Release.

Xem [CONTRIBUTING.md](CONTRIBUTING.md) để biết quy trình đóng góp.

---

## 🤝 Đóng góp

Rất hoan nghênh đóng góp. Vui lòng đọc [Hướng dẫn đóng góp](CONTRIBUTING.md) và
[Quy tắc ứng xử](CODE_OF_CONDUCT.md) trước khi mở pull request.

---

## 📄 Giấy phép

Phát hành theo **Giấy phép MIT**. Xem [LICENSE](LICENSE).
