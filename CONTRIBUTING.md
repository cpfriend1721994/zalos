# Contributing to Zalos

Thank you for your interest in contributing to **Zalos**! Contributions from the community are welcome.

---

## 📜 Code of Conduct

Please review our [Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project, you agree to abide by
its terms.

---

## 🛠️ Development setup

Builds and tests run inside containers, so you do not install a toolchain on your host — this keeps results
reproducible across distributions. You only need `docker` (or `podman`) with Compose, and Git.

```bash
git clone https://github.com/zalos/zalos.git
cd zalos

docker compose run --rm build       # compile
docker compose run --rm test        # unit tests
docker compose run --rm gui-check   # headless GUI sanity check
```

Please run builds and tests through `docker compose` / `podman compose` rather than on the host directly.

---

## 🚀 How to contribute

### Reporting bugs
Before filing an issue, check the existing GitHub issues. A good bug report includes:

- Operating system, kernel version, and desktop environment (GNOME, KDE Plasma, XFCE…).
- Display server (Wayland or X11 — `echo $XDG_SESSION_TYPE`).
- Which package you installed (AppImage, `.deb`, `.rpm`, Arch, Nix).
- Steps to reproduce, and any terminal logs.

### Suggesting features
Open a feature request issue describing the problem you want solved and how you imagine it working.

### Submitting pull requests
1. Fork the repository and create a feature branch:
   ```bash
   git checkout -b feat/your-feature-name
   ```
2. Keep changes focused — don't mix unrelated refactors into one PR.
3. Add unit tests under `tests/` when you add or change behaviour.
4. Make sure the suite and the GUI check pass:
   ```bash
   docker compose run --rm test
   docker compose run --rm gui-check
   ```
5. Use [Conventional Commits](https://www.conventionalcommits.org/):
   ```text
   feat: add media player controls
   fix: handle fractional scaling on the Wayland titlebar
   docs: update the installation guide
   ```
6. Push to your fork and open a Pull Request.

---

## 📐 Coding guidelines

- **C++:** modern C++17 (or C++20 where available); use RAII for resource management; keep logic out of the
  presentation layer and expose data to QML through `Q_PROPERTY` and item models; move heavy work off the UI
  thread.
- **QML:** declarative bindings and responsive layouts; avoid complex logic inside `.qml` files; every component
  must support both light and dark themes.
- **Language:** end-user UI strings must be in **Vietnamese (Tiếng Việt)**; code, comments, commit messages, docs
  and pull-request discussions must be in **English**.
