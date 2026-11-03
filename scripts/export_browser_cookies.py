#!/usr/bin/env python3
"""Export the Zalo Web credentials of a Chromium-based browser that is signed in to chat.zalo.me.

Output (stdout, JSON):
    {"success": true, "browser": "...", "cookies": [{name, value, domain, path, expires}],
     "imei": "<z_uuid>", "userAgent": "..."}
or  {"success": false, "reason": "..."}

Only credentials are exported. Conversations, contacts and messages are always fetched
live from Zalo by the application; nothing is read or synthesised here.
"""
import ctypes
import ctypes.util
import glob
import hashlib
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile

HOME = os.path.expanduser("~")
CONFIG = os.environ.get("XDG_CONFIG_HOME", os.path.join(HOME, ".config"))

# (display name, profile root, libsecret application attribute, kwallet folder/key name)
BROWSERS = [
    ("Thorium", "thorium", "thorium", "Thorium"),
    ("Chromium", "chromium", "chromium", "Chromium"),
    ("Google Chrome", "google-chrome", "chrome", "Chrome"),
    ("Brave", "BraveSoftware/Brave-Browser", "brave", "Brave"),
    ("Microsoft Edge", "microsoft-edge", "chromium", "Chromium"),
    ("Vivaldi", "vivaldi", "chrome", "Chrome"),
]

IMEI_RE = re.compile(rb"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}-[0-9a-f]{32})")


def fail(reason):
    print(json.dumps({"success": False, "reason": reason}))
    sys.exit(0)


# ---------------------------------------------------------------------------
# Safe Storage key
# ---------------------------------------------------------------------------

def run(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, timeout=10)
        if out.returncode == 0:
            return out.stdout.rstrip(b"\n")
    except (OSError, subprocess.SubprocessError):
        pass
    return None


def safe_storage_secrets(app_attr, kwallet_name):
    """Yield candidate Safe Storage passwords, most likely first."""
    lookups = [
        ["secret-tool", "lookup", "application", app_attr],
        ["secret-tool", "lookup", "xdg:schema", "chrome_libsecret_os_crypt_password_v2", "application", app_attr],
        ["secret-tool", "lookup", "xdg:schema", "chrome_libsecret_os_crypt_password_v1", "application", app_attr],
        ["secret-tool", "lookup", "server", f"{kwallet_name} Keys", "user", f"{kwallet_name} Safe Storage"],
        ["secret-tool", "lookup", "server", "Chromium Keys", "user", "Chromium Safe Storage"],
    ]
    for wallet in ("kdewallet",):
        for tool in ("kwallet-query",):
            lookups.append([tool, "-r", f"{kwallet_name} Safe Storage", "-f", f"{kwallet_name} Keys", wallet])
            lookups.append([tool, "-r", "Chromium Safe Storage", "-f", "Chromium Keys", wallet])
    seen = set()
    for cmd in lookups:
        if not shutil.which(cmd[0]):
            continue
        secret = run(cmd)
        if secret and secret not in seen:
            seen.add(secret)
            yield secret
    # "basic" password store (no keyring available)
    yield b"peanuts"


def derive_key(secret):
    return hashlib.pbkdf2_hmac("sha1", secret, b"saltysalt", 1, 16)


def aes_cbc_decrypt(key, data):
    proc = subprocess.run(
        ["openssl", "enc", "-d", "-aes-128-cbc", "-K", key.hex(), "-iv", (b" " * 16).hex()],
        input=data, capture_output=True,
    )
    return proc.stdout if proc.returncode == 0 else None


# ---------------------------------------------------------------------------
# Cookies
# ---------------------------------------------------------------------------

def read_cookie_rows(profile_dir):
    path = os.path.join(profile_dir, "Cookies")
    if not os.path.exists(path):
        path = os.path.join(profile_dir, "Network", "Cookies")
    if not os.path.exists(path):
        return None, 0
    # Copy to avoid "database is locked" while the browser is running.
    with tempfile.TemporaryDirectory() as tmp:
        copy = os.path.join(tmp, "Cookies")
        shutil.copy2(path, copy)
        conn = sqlite3.connect(copy)
        try:
            version = 0
            try:
                version = int(conn.execute("SELECT value FROM meta WHERE key='version'").fetchone()[0])
            except Exception:
                pass
            rows = conn.execute(
                "SELECT host_key, name, value, encrypted_value, path, expires_utc FROM cookies "
                "WHERE host_key LIKE '%zalo.me'"
            ).fetchall()
        finally:
            conn.close()
    return rows, version


def decrypt_cookies(rows, db_version, key):
    cookies = []
    for host, name, value, enc, path, expires_utc in rows:
        if value:
            plain = value
        elif enc[:3] in (b"v10", b"v11"):
            raw = aes_cbc_decrypt(key, enc[3:])
            if raw is None:
                return None  # wrong key
            if db_version >= 24:
                raw = raw[32:]  # SHA-256(host_key) integrity prefix
            try:
                plain = raw.decode("utf-8")
            except UnicodeDecodeError:
                return None
        else:
            continue
        entry = {"name": name, "value": plain, "domain": host, "path": path or "/"}
        if expires_utc:
            # Chromium epoch: microseconds since 1601-01-01.
            entry["expires"] = int(expires_utc / 1_000_000 - 11644473600)
        cookies.append(entry)
    return cookies


# ---------------------------------------------------------------------------
# Device id (localStorage "z_uuid" of https://chat.zalo.me)
# ---------------------------------------------------------------------------

def load_snappy():
    names = [ctypes.util.find_library("snappy"), "libsnappy.so.1", "/usr/lib64/libsnappy.so.1",
             "/usr/lib/x86_64-linux-gnu/libsnappy.so.1"]
    for name in names:
        if not name:
            continue
        try:
            lib = ctypes.cdll.LoadLibrary(name)
        except OSError:
            continue
        lib.snappy_uncompressed_length.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        lib.snappy_uncompress.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p,
                                          ctypes.POINTER(ctypes.c_size_t)]

        def decompress(data, lib=lib):
            n = ctypes.c_size_t()
            if lib.snappy_uncompressed_length(data, len(data), ctypes.byref(n)) != 0:
                return b""
            buf = ctypes.create_string_buffer(n.value)
            if lib.snappy_uncompress(data, len(data), buf, ctypes.byref(n)) != 0:
                return b""
            return buf.raw[: n.value]

        return decompress
    return None


def varint(buf, pos):
    result = shift = 0
    while pos < len(buf):
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            break
        shift += 7
    return result, pos


def ldb_blocks(data, decompress):
    """Yield uncompressed data blocks of a LevelDB table file."""
    if len(data) < 48 or data[-8:] != b"\x57\xfb\x80\x8b\x24\x75\x47\xdb":
        return
    footer = data[-48:]
    _, p = varint(footer, 0)
    _, p = varint(footer, p)
    index_off, p = varint(footer, p)
    index_size, _ = varint(footer, p)

    def block(off, size):
        raw = data[off: off + size]
        kind = data[off + size] if off + size < len(data) else 0
        if kind == 1:
            return decompress(raw) if decompress else b""
        return raw

    index = block(index_off, index_size)
    p = 0
    while p < len(index) - 4:
        try:
            _, p = varint(index, p)
            unshared, p = varint(index, p)
            vlen, p = varint(index, p)
            p += unshared
            val = index[p: p + vlen]
            p += vlen
            off, vp = varint(val, 0)
            size, _ = varint(val, vp)
            yield block(off, size)
        except Exception:
            return


def find_imei(profile_dir):
    ls_dir = os.path.join(profile_dir, "Local Storage", "leveldb")
    if not os.path.isdir(ls_dir):
        return None
    decompress = load_snappy()
    # Newest files first: the most recent write wins.
    files = sorted(glob.glob(os.path.join(ls_dir, "*.log")) + glob.glob(os.path.join(ls_dir, "*.ldb")),
                   key=os.path.getmtime, reverse=True)
    for path in files:
        try:
            with open(path, "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        chunks = [data] if path.endswith(".log") else list(ldb_blocks(data, decompress))
        for chunk in chunks:
            for marker in (b"z_uuid", b"z\x00_\x00u\x00u\x00i\x00d"):
                idx = chunk.find(marker)
                while idx != -1:
                    window = chunk[idx: idx + 220].replace(b"\x00", b"")
                    match = IMEI_RE.search(window)
                    if match:
                        return match.group(1).decode("ascii")
                    idx = chunk.find(marker, idx + 1)
    return None


# ---------------------------------------------------------------------------
# User agent matching the imei suffix (imei = uuid + "-" + md5(userAgent))
# ---------------------------------------------------------------------------

def browser_major_version(root):
    try:
        with open(os.path.join(root, "Last Version")) as fh:
            return fh.read().strip().split(".")[0]
    except OSError:
        return None


def guess_user_agent(imei, root):
    suffix = imei.rsplit("-", 1)[-1]
    versions = []
    major = browser_major_version(root)
    if major:
        versions.append(major)
    versions += [str(v) for v in range(150, 110, -1) if str(v) not in versions]
    platforms = ["X11; Linux x86_64", "Windows NT 10.0; Win64; x64", "Macintosh; Intel Mac OS X 10_15_7"]
    for v in versions:
        for plat in platforms:
            ua = f"Mozilla/5.0 ({plat}) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/{v}.0.0.0 Safari/537.36"
            if hashlib.md5(ua.encode()).hexdigest() == suffix:
                return ua
    plat = platforms[0]
    return f"Mozilla/5.0 ({plat}) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/{major or '130'}.0.0.0 Safari/537.36"


# ---------------------------------------------------------------------------

def main():
    if not shutil.which("openssl"):
        fail("openssl_missing")

    last_reason = "no_browser_session"
    for display, rel, app_attr, kw_name in BROWSERS:
        root = os.path.join(CONFIG, rel)
        profiles = [d for d in [os.path.join(root, "Default")] + sorted(glob.glob(os.path.join(root, "Profile *")))
                    if os.path.isdir(d)]
        for profile in profiles:
            rows, version = read_cookie_rows(profile)
            if not rows or not any(r[1] == "zpw_sek" for r in rows):
                continue
            cookies = None
            for secret in safe_storage_secrets(app_attr, kw_name):
                cookies = decrypt_cookies(rows, version, derive_key(secret))
                if cookies:
                    break
            if not cookies:
                last_reason = "no_secret_key"
                continue
            imei = find_imei(profile)
            if not imei:
                last_reason = "no_device_id"
                continue
            print(json.dumps({
                "success": True,
                "browser": display,
                "cookies": cookies,
                "imei": imei,
                "userAgent": guess_user_agent(imei, root),
            }))
            return
    fail(last_reason)


if __name__ == "__main__":
    main()
