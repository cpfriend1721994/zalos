# Security Policy

## 🔒 Supported Versions

We provide security updates and patches for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0.0 | :x:                |

---

## 🛡️ Reporting a Vulnerability

Security is paramount, especially for an end-to-end encrypted messaging client.

If you discover a security vulnerability in Zalos (such as a memory safety issue, crypto implementation flaw in `db-cross-v4`, or credential exposure), **please do not open a public GitHub issue**.

Instead, please report the vulnerability privately via:
1. **GitHub Private Vulnerability Reporting** directly on the repository tab.
2. Or email the core security team at `security@zalos.local` with:
   - Vulnerability description and attack vector.
   - Proof of Concept (PoC) or reproducible steps.
   - Estimated impact on confidentiality, integrity, or system security.

We will acknowledge receipt within 48 hours and work with you on a coordinated public disclosure timeline once a fix is released.

---

## 🔑 Cryptographic Architecture & Local Security Scope

- **End-to-End Encryption (E2EE)**:
  `Zalos` uses clean-room reverse-engineered C++ implementations of AES-256-CBC, OpenSSL EVP, and LZMA decompression (`src/core/crypto/db_cross.cpp`). Keys are derived locally in memory and never transmitted to external third-party servers.
- **SQLite Decryption**:
  Offline message backups are decrypted locally on your machine.
- **Sandboxing**:
  When using Flatpak, access to filesystem locations outside `$XDG_DOWNLOAD_DIR` is strictly prevented by bubblewrap sandboxing.

