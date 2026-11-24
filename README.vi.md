# Zalos 🚀

<div align="center">

**Ứng dụng Zalo Desktop native cho Linux — thuần Qt 6, hiệu năng cao & siêu nhẹ**

[![CI/CD Pipeline](https://github.com/cpfriend1721994/zalos/actions/workflows/ci.yml/badge.svg)](https://github.com/cpfriend1721994/zalos/actions/workflows/ci.yml)
[![Latest Release](https://img.shields.io/github/v/release/cpfriend1721994/zalos?color=blue)](https://github.com/cpfriend1721994/zalos/releases/latest)
[![Total Downloads](https://img.shields.io/github/downloads/cpfriend1721994/zalos/total?color=success)](https://github.com/cpfriend1721994/zalos/releases)
[![GitHub Stars](https://img.shields.io/github/stars/cpfriend1721994/zalos?style=flat&color=yellow)](https://github.com/cpfriend1721994/zalos/stargazers)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Wayland%20%7C%20X11-purple.svg)](#-tính-năng)
[![Architecture](https://img.shields.io/badge/Arch-x86__64%20%7C%20aarch64%20%7C%20armhf%20%7C%20riscv64-teal.svg)](#-cài-đặt)
[![Packaging](https://img.shields.io/badge/Packaging-deb%20%7C%20RPM%20%7C%20Snap%20%7C%20Flatpak%20%7C%20AppImage%20%7C%20Nix-orange.svg)](#-cài-đặt)
[![zalos](https://snapcraft.io/zalos/badge.svg)](https://snapcraft.io/zalos)

[English](README.md) | [Tiếng Việt](README.vi.md) | [Trang chủ](https://zalos.pages.dev/)

</div>

---

## 📖 Giới thiệu

**Zalos** là ứng dụng mã nguồn mở dành cho Zalo trên Linux, được xây dựng bằng C++17 và Qt 6 QML, kết hợp giữa **hiệu năng cao** (high performance) và **dung lượng siêu nhẹ** (ultra-lightweight):

- **Độc lập & Gọn nhẹ:** Gói cài đặt nhỏ (`~25 MB` `.deb`, `~27 MB` `.rpm`, `~27 MB` `AppImage`) kèm sẵn runtime Qt 6.8.3 LTS biên dịch QML AOT, không cần Qt hệ thống trên mọi distro, cùng mức RAM tối giản chỉ **~40–65 MB** (tiết kiệm hơn 90% bộ nhớ so với 450–1.800+ MB của bản Electron).
- **Hiệu năng cao (High performance):** Khởi động tức thì dưới 0,5 giây, giao diện QML biên dịch trước thành mã máy C++ (`-DZALOS_QML_AOT=ON`), hiển thị đủ emoji màu (font fallback CBDT tích hợp) và hiệu ứng bo góc/làm mờ `MultiEffect`, giải mã mã hóa đa luồng C++ và phát đa phương tiện FFmpeg 7.1.5 LGPL tối giản (H.264/AAC/MP3) tích hợp sẵn.
- **Tự nhiên trên Linux:** Chạy mượt mà trên cả Wayland và X11, tự động đồng bộ giao diện sáng/tối theo hệ thống, tích hợp khay hệ thống và hỗ trợ bộ gõ tiếng Việt Fcitx5 / IBus.

### ⚡ So sánh hiệu năng & tài nguyên thực tế

| Tiêu chí kỹ thuật | Zalos (C++17 / Qt 6 QML) | Zalo PC / Electron Wrapper |
|---|---|---|
| **Bộ nhớ RAM hoạt động** | **~40 – 65 MB** (tự giải phóng, nhẹ hơn 90%) | 450 – 1.800+ MB (nhiều tiến trình Chromium) |
| **Dung lượng gói cài đặt** | **~25 MB (`.deb`) · ~27 MB (`.rpm`) · ~27 MB (AppImage, gồm sẵn Qt 6.8.3)** | > 150 – 250 MB (kèm nguyên trình duyệt) |
| **Tốc độ khởi động nguội** | **< 0,5s** (mở tức thì trong chớp mắt) | 3 – 8s (chờ tải runtime Node/Chromium) |
| **Kết xuất đồ họa UI** | **Qt Quick Native (mặc định phần mềm, tùy chọn GPU)** | Web DOM / Chromium compositor |
| **Kiến trúc mã nguồn** | **C++17 & QML thuần native** | Electron / Node.js + Web wrapper |

---

## ✨ Tính năng

- **Hiệu năng cao & QML AOT** — khởi động tức thì (<0,5s), giao diện 60 FPS biên dịch AOT trên Qt 6.8.3 LTS đóng gói sẵn (~25–27 MB, không cần Qt hệ thống), hỗ trợ sẵn emoji màu + `MultiEffect` và chỉ chiếm ~40–65 MB RAM.
- **Wayland & X11** — chạy trên cả hai, nút điều khiển cửa sổ chuẩn trên GNOME và KDE. Mặc định kết xuất bằng phần mềm cho ổn định; có thể bật tăng tốc phần cứng (GPU) trong Cài đặt và Zalos tự quay về chế độ phần mềm nếu driver đồ họa lỗi.
- **Tự đổi giao diện sáng/tối** — bám theo màu và font chữ của desktop theo thời gian thực trên cả GNOME và KDE; dùng Noto Sans khi font của desktop không có hoặc không đủ tiếng Việt.
- **Nhắn tin** — chat đơn và nhóm, Sticker & ảnh GIF động, emoji, 54 cảm xúc Zalo, định dạng chữ (kèm thụt lề), trả lời, chuyển tiếp,
  thẻ ngân hàng, xem trước bản đồ vị trí kèm ghim avatar, hiển thị đủ chiều ngang ảnh rộng, ghim tin nhắn, nhãn hội thoại (nạp tức thì từ cache), bộ lọc phân loại hội thoại (Tất cả / Nhóm / Cá nhân / Bạn bè / Người lạ), và tìm kiếm toàn bộ lịch sử;
  trạng thái đã đọc đồng bộ với điện thoại và các thiết bị khác.
- **Bình chọn như Zalo PC** — thẻ bình chọn và dòng hoạt động ngay trong chat, cửa sổ bình chọn đầy đủ (chọn một/nhiều, đổi hoặc bỏ bình chọn,
  thêm phương án, xem ai đã chọn), đủ thiết lập khi tạo (ghim, thời hạn, ẩn kết quả, ẩn người bình chọn) và khóa bình chọn cho người tạo/quản trị viên.
- **Đồng bộ lịch sử điện thoại thông minh** — tự nhận diện khi nhóm chưa có tin nhắn sau đăng nhập để gợi ý đồng bộ mã hóa từ điện thoại.
- **Gọi thoại và gọi video** — gọi thoại/video 1-1 với mã hóa OpenH264, chia sẻ màn hình (X11 & Wayland), thu nhỏ nổi PiP để vừa gọi vừa chat, chọn Micro/Loa/Camera + thanh âm lượng & nút chuyển độ lợi Micro Tự động/Thủ công, cùng cơ chế giữ kết nối khi đổ chuông lâu và tự chuyển mạng P2P/relay; gọi nhóm đang ở giai đoạn thử nghiệm.
- **My Documents** — luồng lưu trữ cá nhân với bộ lọc nhanh ảnh, video, tệp và liên kết.
- **Tiện ích máy tính** — chụp màn hình (`Ctrl+Alt+S`), trợ lý Kiki (có thể ẩn/hiện) và công cụ zBusiness đầy đủ API (tra cứu hồ sơ, quản lý danh mục & sản phẩm đồng bộ máy chủ, quy tắc trả lời tự động và tin nhắn nhanh).
- **Danh bạ & nhóm** — 1 cú nhấp gọi thoại, gọi video, nhắn tin trên mỗi liên hệ; quản lý lời mời nhóm, gợi ý kết bạn, xem nhóm chung trong hồ sơ và nâng cấp nhóm lên Cộng đồng.
- **An toàn & thiết bị** — kiểm tra liên kết an toàn (`scanURL`) và cảnh báo khi Zalo đánh dấu link không an toàn (kiểm tra chậm không chặn mở link quá 2,5 giây), báo cáo vi phạm từ tin nhắn/hồ sơ và xem thiết bị liên kết E2EE trong Cài đặt.
- **Cảm xúc (Reactions)** — thông báo khi có người bày tỏ cảm xúc với tin nhắn của bạn (tắt được trong Cài đặt), và nút trên khung chat để lần lượt đi tới các tin có cảm xúc chưa xem, từ cũ đến mới.
- **Hộp thoại chọn tệp gốc** — chọn ảnh, tệp, ảnh đại diện, ảnh nền và nơi lưu mở hộp thoại của chính desktop qua XDG portal (dự phòng `zenity`/`kdialog`).
- **Khay hệ thống** — hiển thị số tin chưa đọc, huy hiệu trạng thái gọi và menu điều khiển cuộc gọi (tắt mic, loa, camera, chia sẻ màn hình, kết thúc) kèm cơ chế tự thu nhỏ khi desktop không hỗ trợ khay.
- **Lịch sử ngoại tuyến** — xem lại hội thoại cả khi không có kết nối.
- **Cập nhật tiện lợi theo bản cài** — kiểm tra phát hành từ GitHub kèm đối chiếu SHA-256 (tự cập nhật AppImage + `gh-releases-zsync`, cài qua `pkexec`/trình cài hệ thống cho `.deb`/`.rpm`).
- **Giao diện song ngữ** — chuyển đổi tức thì giữa Tiếng Việt và Tiếng Anh.

> **Miễn trừ trách nhiệm:** Zalos là một dự án cộng đồng mã nguồn mở, **không** liên kết, không được chứng thực hay
> tài trợ bởi Công ty VNG hoặc Zalo. Mọi thương hiệu và logo thuộc về chủ sở hữu tương ứng.

---

## 📥 Cài đặt

Chọn gói theo bản phân phối của bạn. Mỗi bản phát hành được build trên bản phân phối **cũ nhất** mà nó hỗ trợ,
Trước mỗi bản phát hành, kiểm tra cài đặt sạch được chạy cục bộ trong container trên cả bản cũ nhất lẫn mới nhất (kể cả cài tối giản không kèm gói tùy chọn); CI chỉ build và phát hành gói
trước khi phát hành.

### Bảng ma trận cài đặt (Install Matrix)

| Cách cài đặt | Hệ điều hành hỗ trợ | Kiến trúc | Tự cập nhật | Gọi thoại | Gọi video (Camera) |
|---|---|---|---|---|---|
| [**Kho APT**](#ubuntu--debian--kho-apt-khuyên-dùng) *(khuyên dùng)* | Ubuntu 24.04+, Mint, Pop!_OS, Zorin | `amd64` / `arm64` | ✅ `apt upgrade` | ✅ Dùng luôn | ✅ đề xuất sẵn (`libopenh264-7`) |
| [**Kho DNF**](#fedora--kho-dnf-khuyên-dùng) *(khuyên dùng)* | Fedora 43+ | `x86_64` / `aarch64` | ✅ `dnf upgrade` | ✅ Dùng luôn | ✅ đề xuất sẵn (`openh264`) |
| [File `.deb` lẻ](#ubuntu-2404-file-deb-lẻ) | Ubuntu 24.04+, Debian & phái sinh | `amd64` / `arm64` | 🔄 Báo trong app | ✅ Dùng luôn | ✅ đề xuất sẵn (`libopenh264-7`) |
| [File `.rpm` lẻ](#fedora-43-file-rpm-lẻ) | Fedora 43+ | `x86_64` / `aarch64` | 🔄 Báo trong app | ✅ Dùng luôn | ✅ đề xuất sẵn (`openh264`) |
| [**Snap**](#snap) | Ubuntu & distro có `snapd` | `amd64` / `arm64` / `armhf` / `riscv64` | ✅ Snap Store | ✅ Dùng luôn | ⚠️ Cấp quyền mic/cam |
| [**Flatpak (Testing)**](#flatpak-testing) | Mọi distro có Flatpak | `x86_64` / `aarch64` | ✅ Flatpak | ✅ Dùng luôn | ✅ Đóng gói sẵn (`OpenH264`) |
| [**Nix Flake**](#nixos--nix) | NixOS / Nix | `x86_64` / `aarch64` | 🔄 `nix profile upgrade` | ✅ Dùng luôn | ✅ Đóng gói sẵn |
| [**AppImage**](#appimage-di-động-glibc--235) | Mọi distro hiện đại (`glibc ≥ 2.35`: Ubuntu 22.04+, Debian 12+, Fedora, Arch, openSUSE) | `x86_64` / `aarch64` | ✅ `zsync` / trong app | ✅ Dùng luôn *(desktop)* | ➕ `libopenh264-7` / `openh264` |

### Ubuntu / Debian — Kho APT (khuyên dùng)
```bash
curl -fsSL https://cpfriend1721994.github.io/zalos/zalos.gpg | sudo gpg --dearmor -o /usr/share/keyrings/zalos.gpg
echo "deb [signed-by=/usr/share/keyrings/zalos.gpg] https://cpfriend1721994.github.io/zalos/apt stable main" | sudo tee /etc/apt/sources.list.d/zalos.list
sudo apt update && sudo apt install -y zalos
```

### Fedora — Kho DNF (khuyên dùng)
```bash
sudo curl -fsSL https://cpfriend1721994.github.io/zalos/zalos.repo -o /etc/yum.repos.d/zalos.repo
sudo dnf install -y zalos
```

### Ubuntu 24.04+ (file `.deb` lẻ)
```bash
ARCH=$(dpkg --print-architecture)
wget "https://github.com/cpfriend1721994/zalos/releases/latest/download/zalos_1.3.0-1~ubuntu24.04_${ARCH}.deb"
sudo apt install "./zalos_1.3.0-1~ubuntu24.04_${ARCH}.deb"
```

### Fedora 43+ (file `.rpm` lẻ)
```bash
sudo dnf install "https://github.com/cpfriend1721994/zalos/releases/latest/download/zalos-1.3.0-1.fc43.$(uname -m).rpm"
```

### Snap
Hỗ trợ các kiến trúc `amd64`, `arm64`, `armhf` và `riscv64` ([Snap Store](https://snapcraft.io/zalos)):
```bash
sudo snap install zalos
# Cấp quyền camera & micro cho cuộc gọi nếu chưa tự kết nối:
sudo snap connect zalos:camera
sudo snap connect zalos:audio-record
```

### Flatpak (Testing)
Hỗ trợ cả `x86_64` và `aarch64` ([xem trước trên Flathub](https://builds.flathub.org/appstream/afeb93f6-0f7e-42bb-a97f-191a935cacd4)):
```bash
flatpak install --user https://dl.flathub.org/build-repo/342380/io.github.cpfriend1721994.zalos.flatpakref
flatpak run io.github.cpfriend1721994.zalos
```

### NixOS / Nix
```bash
nix run github:cpfriend1721994/zalos     # chạy thử không cài
nix profile install github:cpfriend1721994/zalos
```

### AppImage (di động, glibc ≥ 2.35)
```bash
ARCH=$(uname -m)
wget "https://github.com/cpfriend1721994/zalos/releases/latest/download/Zalos-1.3.0-${ARCH}.AppImage"
chmod +x "Zalos-1.3.0-${ARCH}.AppImage"
"./Zalos-1.3.0-${ARCH}.AppImage"
```
AppImage (~27 MB) đóng gói sẵn runtime Qt 6.8.3 LTS biên dịch QML AOT, font emoji màu fallback và FFmpeg 7.1.5 LGPL tối giản — hoàn toàn không cần Qt hệ thống (chạy trơn tru trên Ubuntu 22.04+, Debian 12+, Fedora, openSUSE, Arch với `glibc ≥ 2.35`).

### Arch Linux & các bản phân phối khác
Dự án không phát hành gói dựng sẵn cho Arch Linux. Bạn có thể dùng **Flatpak**, **Snap**, **AppImage** di động ở trên, hoặc **tự build từ mã nguồn** — clone kho này và làm theo
[Build từ mã nguồn](#️-build-từ-mã-nguồn).

### Tuỳ chọn: cuộc gọi
Gọi thoại và video chạy trên engine gọi C++ của chính Zalos (`zalos-call` và `zalos-call-audio`, nằm trong `libexec/zalos`;
xem `engines/zcall-cpp/LICENSE` cho thông báo giấy phép BSD-3-Clause).
Zalos không phụ thuộc Python hay Node.js:

- **Gọi thoại hoạt động ngay** trên `.deb`, `.rpm`, `Nix` và **AppImage**: Zalos dùng `libopus` của hệ thống và tự dùng bản dự phòng riêng khi hệ thống không có, kết nối PulseAudio / PipeWire qua `libpulse-simple.so.0` vốn có sẵn trên mọi bản desktop (chỉ hệ thống tối giản mới cần thêm `libpulse0` trên Ubuntu/Debian hoặc `pulseaudio-libs` trên Fedora).
- **Gọi video** (thấy camera của người bên kia) cần thêm **OpenH264** từ bản phân phối của bạn (không bao giờ đóng gói kèm, vì lý do bản quyền sáng chế): Ubuntu `sudo apt install libopenh264-7`, Fedora `sudo dnf install openh264`. Gói `.deb`/`.rpm` đã đề xuất sẵn nên `apt`/`dnf` mặc định sẽ cài kèm.
- **Phát tin nhắn thoại và video** chạy qua Qt Multimedia dùng backend FFmpeg tích hợp sẵn trong Qt 6.8.3 (hoạt động ngay trên cả `.deb`, `.rpm` và AppImage; nếu thiếu thiết bị âm thanh hoặc codec bổ sung, Zalos vẫn chạy bình thường và báo rõ trạng thái).
- **Kiểm tra Zalos đang dùng gì:** `zalos --check-deps` (hoặc `./Zalos-*.AppImage --check-deps`) in ra Qt, codec, OpenH264 và thư viện âm thanh đang được dùng — tiện khi báo lỗi.

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
docker compose run --rm package-rpm      # dist/*.deb + dist/*.x86_64.rpm (.rpm repackaged from the .deb build)
docker compose run --rm package-nix      # dist/*-nixos.tar.gz
```

Đẩy một tag theo chuẩn semantic (`git tag v1.3.0 && git push origin v1.3.0`) sẽ build toàn bộ gói và phát hành
một GitHub Release.

Xem [CONTRIBUTING.md](CONTRIBUTING.md) để biết quy trình đóng góp.

---

## 🤝 Đóng góp

Rất hoan nghênh đóng góp. Vui lòng đọc [Hướng dẫn đóng góp](CONTRIBUTING.md) và
[Quy tắc ứng xử](CODE_OF_CONDUCT.md) trước khi mở pull request.

---

## 📄 Giấy phép

Phát hành theo **Giấy phép MIT**. Xem [LICENSE](LICENSE).
