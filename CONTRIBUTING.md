# Contributing to Zalos

Thank you for your interest in contributing to **Zalos**! We welcome contributions from the community to make Zalos the best native desktop messaging experience on Linux.

---

## 📋 Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Zero Host Execution Policy](#zero-host-execution-policy)
- [Development Environment Setup](#development-environment-setup)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Suggesting Features](#suggesting-features)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Coding Guidelines](#coding-guidelines)
  - [Modern C++ Standards](#modern-c-standards)
  - [QML Presentation Rules](#qml-presentation-rules)
  - [Commit Message Format](#commit-message-format)

---

## 📜 Code of Conduct

Please review our [Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project, you agree to abide by its terms.

---

## 🛡️ Zero Host Execution Policy

To ensure uniform toolchains, prevent host library collisions, and guarantee reproducibility across distributions, **all development, testing, and packaging MUST be executed inside Docker or Podman containers**:

* ❌ **Do NOT** run `cmake`, `ninja`, `make`, `ctest`, `gcc`, `g++`, or launch binaries directly on your host machine.
* ✅ **Always** use `docker compose` or `podman compose`.

---

## 🛠️ Development Environment Setup

1. **Prerequisites**:
   - `docker` & `docker compose` (or `podman` & `podman compose`).
   - Git.

2. **Clone the repository**:
   ```bash
   git clone https://github.com/zalos/zalos.git
   cd zalos
   ```

3. **Build the container image & project**:
   ```bash
   docker compose run --rm build
   ```

4. **Run unit tests**:
   ```bash
   docker compose run --rm test
   ```

5. **Headless GUI verification**:
   ```bash
   docker compose run --rm gui-check
   ```

---

## 🚀 How to Contribute

### Reporting Bugs
Before filing an issue, check existing GitHub issues. When creating a bug report, please include:
- Operating system, kernel version, and Desktop Environment (GNOME, KDE Plasma, XFCE).
- Display Server (Wayland or X11/Xorg via `echo $XDG_SESSION_TYPE`).
- Distribution package used (AppImage, Flatpak, or RPM).
- Steps to reproduce and terminal/container logs.

### Submitting Pull Requests
1. Fork the repository and create your feature branch:
   ```bash
   git checkout -b feat/your-feature-name
   ```
2. Keep your changes focused. Do not combine unrelated refactorings.
3. Add unit tests in `tests/` if you add new cryptographic, network, or model logic.
4. Verify that all tests pass:
   ```bash
   docker compose run --rm test
   ```
5. Ensure headless GUI sanity check passes:
   ```bash
   docker compose run --rm gui-check
   ```
6. Commit using **Conventional Commits**:
   ```text
   feat: add MPRIS media player controls integration
   fix: handle fractional scaling on Wayland CSD titlebar
   docs: update Flatpak permission checklist
   ```
7. Push to your fork and submit a Pull Request.

---

## 📐 Coding Guidelines

### Modern C++ Standards
- Follow modern **C++17** practices (or C++20 where supported).
- Use RAII for resource management (`std::unique_ptr`, `EVP_CIPHER_CTX_free`, `sqlite3_finalize`).
- Keep controllers decoupled from presentation: expose data via `Q_PROPERTY` and `QAbstractListModel`.
- Threading: Offload heavy I/O or crypto decryption to background worker threads.

### QML Presentation Rules
- Follow Qt Quick best practices: use declarative bindings and responsive layouts (`RowLayout`, `ColumnLayout`).
- Avoid putting complex business logic or string parsing in JavaScript inside `.qml` files.
- Ensure all components support both Dark and Light themes via `ThemeManager`.

### Language Rules
- End-user UI strings, status toasts, and dialogs must strictly be in **Vietnamese (Tiếng Việt)**.
- Code comments, commit messages, docs, and pull request discussions must be in **English**.

