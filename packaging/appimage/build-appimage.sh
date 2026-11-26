#!/usr/bin/env bash
set -eo pipefail

echo "=========================================================="
echo "📦 Building Zalos AppImage"
echo "=========================================================="

WORKSPACE_DIR="${WORKSPACE_DIR:-/workspace}"
ARCH="$(uname -m)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "${ARCH}-linux-gnu")"
BUILD_DIR="${WORKSPACE_DIR}/build-appimage"
APP_DIR="${BUILD_DIR}/AppDir"
OUTPUT_DIR="${WORKSPACE_DIR}/dist"
VERSION=$(grep -m1 'project(zalos VERSION' "${WORKSPACE_DIR}/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9.]+).*/\1/')

mkdir -p "${BUILD_DIR}" "${APP_DIR}" "${OUTPUT_DIR}"

DISTRO_STAMP="$(. /etc/os-release && echo "${ID}-${VERSION_ID}")"
BUILD_JOBS="$(nproc)"
if [ "${ZALOS_EMULATED:-0}" = "1" ] && [ "${BUILD_JOBS}" -gt 5 ]; then
    BUILD_JOBS=5
fi
CMAKE_BUILD_DIR="${BUILD_DIR}"
if [ -x "${WORKSPACE_DIR}/build-deb/zalos" ] && [ -f "${WORKSPACE_DIR}/build-deb/CMakeCache.txt" ] \
   && [ ! "${WORKSPACE_DIR}/CMakeLists.txt" -nt "${WORKSPACE_DIR}/build-deb/CMakeCache.txt" ]; then
    echo "==> Reusing Release build from build-deb..."
    CMAKE_BUILD_DIR="${WORKSPACE_DIR}/build-deb"
    cmake --build "${CMAKE_BUILD_DIR}" --config Release -j"${BUILD_JOBS}"
else
    echo "==> Configuring and compiling Zalos..."
    if [ -f "${CMAKE_BUILD_DIR}/CMakeCache.txt" ] && [ "$(cat "${CMAKE_BUILD_DIR}/.distro" 2>/dev/null)" != "${DISTRO_STAMP}" ]; then
        echo "==> Build tree from another distro: reconfiguring from scratch"
        rm -rf "${CMAKE_BUILD_DIR:?}"
    fi
    mkdir -p "${CMAKE_BUILD_DIR}"
    echo "${DISTRO_STAMP}" > "${CMAKE_BUILD_DIR}/.distro"
    cmake -B "${CMAKE_BUILD_DIR}" -S "${WORKSPACE_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DBUILD_TESTING=OFF \
        -DZALOS_QML_AOT=ON

    cmake --build "${CMAKE_BUILD_DIR}" --config Release -j"${BUILD_JOBS}"
fi
rm -rf "${APP_DIR}"
DESTDIR="${APP_DIR}" cmake --install "${CMAKE_BUILD_DIR}" --strip

echo "==> Bundling matched libxkbcommon + libxkbcommon-x11 (only used when the host lacks libxkbcommon-x11)..."
XKB_DIR="${APP_DIR}/usr/lib/xkb-fallback"
mkdir -p "${XKB_DIR}"
for xkb_lib in libxkbcommon.so.0 libxkbcommon-x11.so.0; do
    xkb_src=""
    for cand in "/usr/lib/${MULTIARCH}/${xkb_lib}" "/lib/${MULTIARCH}/${xkb_lib}" "/usr/lib64/${xkb_lib}" "/usr/lib/${xkb_lib}"; do
        if [ -e "${cand}" ]; then
            xkb_src="${cand}"
            break
        fi
    done
    if [ -z "${xkb_src}" ]; then
        echo "❌ ${xkb_lib} not found in the build image" >&2
        exit 1
    fi
    cp -L "${xkb_src}" "${XKB_DIR}/${xkb_lib}"
done

echo "==> Bundling Qt 6.8.3 LTS runtime..."
QT_FALLBACK="${APP_DIR}/usr/lib/qt6-fallback"
bash "${WORKSPACE_DIR}/packaging/common/bundle-qt6.sh" \
    "${QT_ROOT:-/opt/qt6}" "${QT_FALLBACK}" "${APP_DIR}/usr/bin" "${APP_DIR}/usr/libexec/zalos" --build-icu-shim

SHIM_TMP="$(mktemp -d)"
trap 'rm -rf "${SHIM_TMP}"' EXIT

mkdir -p "${APP_DIR}/usr/lib"
cat > "${SHIM_TMP}/glibc_compat.c" <<'EOF'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

extern long real_strtol(const char *, char **, int) __asm__("strtol");
extern unsigned long real_strtoul(const char *, char **, int) __asm__("strtoul");
extern long long real_strtoll(const char *, char **, int) __asm__("strtoll");
extern unsigned long long real_strtoull(const char *, char **, int) __asm__("strtoull");
extern intmax_t real_strtoimax(const char *, char **, int) __asm__("strtoimax");
extern uintmax_t real_strtoumax(const char *, char **, int) __asm__("strtoumax");
extern int real_vsscanf(const char *, const char *, va_list) __asm__("__isoc99_vsscanf");
extern int real_vfscanf(void *, const char *, va_list) __asm__("__isoc99_vfscanf");

long __isoc23_strtol(const char *s, char **e, int b) { return real_strtol(s, e, b); }
unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return real_strtoul(s, e, b); }
long long __isoc23_strtoll(const char *s, char **e, int b) { return real_strtoll(s, e, b); }
unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return real_strtoull(s, e, b); }
intmax_t __isoc23_strtoimax(const char *s, char **e, int b) { return real_strtoimax(s, e, b); }
uintmax_t __isoc23_strtoumax(const char *s, char **e, int b) { return real_strtoumax(s, e, b); }

int __isoc23_vsscanf(const char *s, const char *fmt, va_list ap) {
    return real_vsscanf(s, fmt, ap);
}

int __isoc23_sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = real_vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

int __isoc23_vfscanf(void *fp, const char *fmt, va_list ap) {
    return real_vfscanf(fp, fmt, ap);
}

int __isoc23_fscanf(void *fp, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = real_vfscanf(fp, fmt, ap);
    va_end(ap);
    return r;
}

void arc4random_buf(void *buf, size_t n) {
    uint8_t *p = (uint8_t *)buf;
    while (n > 0) {
        size_t chunk = n > 256 ? 256 : n;
        if (getentropy(p, chunk) != 0) break;
        p += chunk;
        n -= chunk;
    }
}

uint32_t arc4random(void) {
    uint32_t v = 0;
    arc4random_buf(&v, sizeof(v));
    return v;
}

void __attribute__((constructor(101))) _ZSt21ios_base_library_initv(void) {
    static int done = 0;
    static unsigned long long io_init[8];
    if (__atomic_exchange_n(&done, 1, __ATOMIC_ACQ_REL)) return;
    typedef void (*fn_t)(void);
    fn_t next_fn = (fn_t)dlsym(RTLD_NEXT, "_ZSt21ios_base_library_initv");
    if (next_fn && next_fn != &_ZSt21ios_base_library_initv) {
        next_fn();
        return;
    }
    typedef void (*ctor_t)(void *);
    ctor_t ctor = (ctor_t)dlsym(RTLD_DEFAULT, "_ZNSt8ios_base4InitC1Ev");
    if (ctor) ctor(io_init);
}
EOF
gcc -O2 -shared -fPIC -Wall -Wextra "${SHIM_TMP}/glibc_compat.c" \
    -Wl,-soname,libzalos-glibc-compat.so -ldl -o "${APP_DIR}/usr/lib/libzalos-glibc-compat.so"

cat > "${SHIM_TMP}/elf_compat_patch.c" <<'EOF'
#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int is_too_new_ver(const char *v) {
    int a = 0, b = 0, c = 0;
    if (sscanf(v, "GLIBC_%d.%d.%d", &a, &b, &c) >= 2 || sscanf(v, "GLIBC_%d.%d", &a, &b) == 2) {
        return a > 2 || (a == 2 && b > 35);
    }
    if (sscanf(v, "GLIBCXX_%d.%d.%d", &a, &b, &c) == 3) {
        return a > 3 || (a == 3 && (b > 4 || (b == 4 && c > 30)));
    }
    if (sscanf(v, "CXXABI_%d.%d.%d", &a, &b, &c) == 3) {
        return a > 1 || (a == 1 && (b > 3 || (b == 3 && c > 13)));
    }
    return 0;
}

static int is_allowed_compat_sym(const char *name) {
    if (strcmp(name, "fmod") == 0 || strcmp(name, "fmodf") == 0 || strcmp(name, "fmodl") == 0) return 1;
    if (strncmp(name, "__isoc23_", 9) == 0) return 1;
    if (strncmp(name, "arc4random", 10) == 0) return 1;
    if (strcmp(name, "_ZSt21ios_base_library_initv") == 0) return 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror(argv[1]); return 1; }
    struct stat st;
    if (fstat(fd, &st) != 0) { perror("fstat"); close(fd); return 1; }
    if ((size_t)st.st_size < sizeof(Elf64_Ehdr)) { close(fd); return 0; }

    uint8_t *base = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    if (memcmp(base, ELFMAG, SELFMAG) != 0 || base[EI_CLASS] != ELFCLASS64) {
        munmap(base, (size_t)st.st_size);
        close(fd);
        return 0;
    }

    Elf64_Ehdr *eh = (Elf64_Ehdr *)base;
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) {
        munmap(base, (size_t)st.st_size);
        close(fd);
        return 0;
    }
    Elf64_Shdr *sh = (Elf64_Shdr *)(base + eh->e_shoff);
    Elf64_Shdr *dynsym_sh = NULL, *dynstr_sh = NULL, *versym_sh = NULL, *verneed_sh = NULL, *dynamic_sh = NULL;

    for (int i = 0; i < eh->e_shnum; ++i) {
        if (sh[i].sh_type == SHT_DYNSYM) {
            dynsym_sh = &sh[i];
            if (sh[i].sh_link < eh->e_shnum) dynstr_sh = &sh[sh[i].sh_link];
        } else if (sh[i].sh_type == SHT_GNU_versym) {
            versym_sh = &sh[i];
        } else if (sh[i].sh_type == SHT_GNU_verneed) {
            verneed_sh = &sh[i];
        } else if (sh[i].sh_type == SHT_DYNAMIC) {
            dynamic_sh = &sh[i];
        }
    }

    if (!dynsym_sh || !dynstr_sh || !versym_sh || !verneed_sh) {
        munmap(base, (size_t)st.st_size);
        close(fd);
        return 0;
    }

    const char *dynstr = (const char *)(base + dynstr_sh->sh_offset);
    size_t dynstr_sz = dynstr_sh->sh_size;
    Elf64_Sym *syms = (Elf64_Sym *)(base + dynsym_sh->sh_offset);
    size_t nsyms = dynsym_sh->sh_size / sizeof(Elf64_Sym);
    Elf64_Half *versym = (Elf64_Half *)(base + versym_sh->sh_offset);

    uint8_t bad_idx[65536];
    memset(bad_idx, 0, sizeof(bad_idx));
    int has_bad = 0;

    uint8_t *vn_start = base + verneed_sh->sh_offset;
    size_t vn_sz = verneed_sh->sh_size;
    size_t vn_pos = 0;
    for (Elf64_Word n = 0; n < verneed_sh->sh_info && vn_pos + sizeof(Elf64_Verneed) <= vn_sz; ++n) {
        Elf64_Verneed *vn = (Elf64_Verneed *)(vn_start + vn_pos);
        size_t aux_pos = vn_pos + vn->vn_aux;
        for (Elf64_Half a = 0; a < vn->vn_cnt && aux_pos + sizeof(Elf64_Vernaux) <= vn_sz; ++a) {
            Elf64_Vernaux *aux = (Elf64_Vernaux *)(vn_start + aux_pos);
            if (aux->vna_name < dynstr_sz) {
                const char *vname = dynstr + aux->vna_name;
                if (is_too_new_ver(vname)) {
                    Elf64_Half idx = aux->vna_other & 0x7fff;
                    bad_idx[idx] = 1;
                    has_bad = 1;
                }
            }
            if (aux->vna_next == 0) break;
            aux_pos += aux->vna_next;
        }
        if (vn->vn_next == 0) break;
        vn_pos += vn->vn_next;
    }

    if (!has_bad) {
        munmap(base, (size_t)st.st_size);
        close(fd);
        return 0;
    }

    for (size_t i = 0; i < nsyms; ++i) {
        if (syms[i].st_shndx != SHN_UNDEF) continue;
        Elf64_Half idx = versym[i] & 0x7fff;
        if (!bad_idx[idx]) continue;
        const char *sname = (syms[i].st_name < dynstr_sz) ? (dynstr + syms[i].st_name) : "";
        if (!is_allowed_compat_sym(sname)) {
            fprintf(stderr, "❌ Unhandled newer glibc/GLIBCXX symbol in %s: %s\n", argv[1], sname);
            munmap(base, (size_t)st.st_size);
            close(fd);
            return 1;
        }
        versym[i] = 1;
    }

    uint8_t *rebuilt = (uint8_t *)calloc(1, vn_sz);
    if (!rebuilt) { munmap(base, (size_t)st.st_size); close(fd); return 1; }

    size_t out_pos = 0;
    Elf64_Word kept_vn_cnt = 0;
    Elf64_Verneed *prev_out_vn = NULL;

    vn_pos = 0;
    for (Elf64_Word n = 0; n < verneed_sh->sh_info && vn_pos + sizeof(Elf64_Verneed) <= vn_sz; ++n) {
        Elf64_Verneed *vn = (Elf64_Verneed *)(vn_start + vn_pos);
        Elf64_Verneed *out_vn = (Elf64_Verneed *)(rebuilt + out_pos);
        size_t aux_write = out_pos + sizeof(Elf64_Verneed);
        Elf64_Half kept_aux = 0;
        Elf64_Vernaux *prev_out_aux = NULL;

        size_t aux_pos = vn_pos + vn->vn_aux;
        for (Elf64_Half a = 0; a < vn->vn_cnt && aux_pos + sizeof(Elf64_Vernaux) <= vn_sz; ++a) {
            Elf64_Vernaux *aux = (Elf64_Vernaux *)(vn_start + aux_pos);
            Elf64_Half idx = aux->vna_other & 0x7fff;
            if (!bad_idx[idx]) {
                Elf64_Vernaux *dst = (Elf64_Vernaux *)(rebuilt + aux_write);
                *dst = *aux;
                dst->vna_next = 0;
                if (prev_out_aux) {
                    prev_out_aux->vna_next = (Elf64_Word)((uint8_t *)dst - (uint8_t *)prev_out_aux);
                }
                prev_out_aux = dst;
                aux_write += sizeof(Elf64_Vernaux);
                kept_aux++;
            }
            if (aux->vna_next == 0) break;
            aux_pos += aux->vna_next;
        }

        if (kept_aux > 0) {
            *out_vn = *vn;
            out_vn->vn_cnt = kept_aux;
            out_vn->vn_aux = (Elf64_Word)sizeof(Elf64_Verneed);
            out_vn->vn_next = 0;
            if (prev_out_vn) {
                prev_out_vn->vn_next = (Elf64_Word)((uint8_t *)out_vn - (uint8_t *)prev_out_vn);
            }
            prev_out_vn = out_vn;
            out_pos = aux_write;
            kept_vn_cnt++;
        }

        if (vn->vn_next == 0) break;
        vn_pos += vn->vn_next;
    }

    memcpy(vn_start, rebuilt, vn_sz);
    free(rebuilt);
    verneed_sh->sh_info = kept_vn_cnt;

    if (dynamic_sh) {
        Elf64_Dyn *dyn = (Elf64_Dyn *)(base + dynamic_sh->sh_offset);
        size_t ndyn = dynamic_sh->sh_size / sizeof(Elf64_Dyn);
        if (kept_vn_cnt > 0) {
            for (size_t i = 0; i < ndyn && dyn[i].d_tag != DT_NULL; ++i) {
                if (dyn[i].d_tag == DT_VERNEEDNUM) {
                    dyn[i].d_un.d_val = kept_vn_cnt;
                }
            }
        } else {
            size_t w = 0;
            for (size_t i = 0; i < ndyn && dyn[i].d_tag != DT_NULL; ++i) {
                if (dyn[i].d_tag == DT_VERNEED || dyn[i].d_tag == DT_VERNEEDNUM) continue;
                dyn[w++] = dyn[i];
            }
            while (w < ndyn) {
                dyn[w].d_tag = DT_NULL;
                dyn[w].d_un.d_val = 0;
                w++;
            }
        }
    }

    msync(base, (size_t)st.st_size, MS_SYNC);
    munmap(base, (size_t)st.st_size);
    close(fd);
    return 0;
}
EOF
gcc -O2 -Wall -Wextra "${SHIM_TMP}/elf_compat_patch.c" -o "${SHIM_TMP}/elf-compat-patch"

echo "==> Patching RPATHs & scrubbing glibc > 2.35 / GLIBCXX > 3.4.30 references..."
python3 - "${APP_DIR}" "${QT_FALLBACK}" "${SHIM_TMP}/elf-compat-patch" <<'PY'
import os, re, subprocess, sys

app_dir, qt_fallback, patcher = sys.argv[1:4]
qt_lib = os.path.join(qt_fallback, "lib")
usr_lib = os.path.join(app_dir, "usr", "lib")

compat_sym_re = re.compile(r"\*UND\*.*\b(__isoc23_[A-Za-z0-9_]+|arc4random[A-Za-z0-9_]*|_ZSt21ios_base_library_initv)\b")

def is_elf(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    try:
        with open(path, "rb") as fp:
            return fp.read(4) == b"\x7fELF"
    except OSError:
        return False

for root_dir, _, files in os.walk(app_dir):
    for fn in sorted(files):
        p = os.path.join(root_dir, fn)
        if not is_elf(p):
            continue
        bn = os.path.basename(p)
        if bn in ("libzalos-glibc-compat.so", "libproxy.so.1") or bn.startswith("libicu"):
            continue

        rel_from_dir = os.path.relpath(usr_lib, root_dir)
        rpath_usr = "$ORIGIN" if rel_from_dir == "." else "$ORIGIN/" + rel_from_dir

        if p.startswith(qt_fallback + "/") or p == os.path.join(app_dir, "usr", "bin", "zalos") or p.startswith(os.path.join(app_dir, "usr", "libexec", "zalos") + "/"):
            rel_qt = os.path.relpath(qt_lib, root_dir)
            rpath_qt = "$ORIGIN" if rel_qt == "." else "$ORIGIN/" + rel_qt
            rpath = f"{rpath_qt}:{rpath_usr}"
        else:
            rpath = rpath_usr

        subprocess.check_call(["patchelf", "--set-rpath", rpath, p])

        need_compat = (
            p == os.path.join(app_dir, "usr", "bin", "zalos")
            or p.startswith(os.path.join(app_dir, "usr", "libexec", "zalos") + "/")
        )
        if not need_compat:
            und = subprocess.check_output(["objdump", "-T", p], text=True, stderr=subprocess.DEVNULL)
            need_compat = bool(compat_sym_re.search(und))
        if need_compat:
            needed = subprocess.check_output(["patchelf", "--print-needed", p], text=True).splitlines()
            if "libzalos-glibc-compat.so" not in needed:
                subprocess.check_call(["patchelf", "--add-needed", "libzalos-glibc-compat.so", p])

        subprocess.check_call([patcher, p])

        if ".so" in bn:
            subprocess.run(["strip", "--strip-unneeded", "--remove-section=.comment", p], check=False)
PY

for _shim in "${APP_DIR}/usr/lib/libzalos-glibc-compat.so" "${QT_FALLBACK}/lib/libicu"*.so* "${QT_FALLBACK}/lib/libproxy.so.1"; do
    [ -f "${_shim}" ] && strip --strip-unneeded --remove-section=.comment "${_shim}"
done

BAD_SYMS="$(find "${APP_DIR}" -type f -exec sh -c 'objdump -T "$1" 2>/dev/null | awk "/\*UND\*/ && /(GLIBC_2\.(3[6-9]|[4-9][0-9])|GLIBCXX_3\.4\.(3[1-9]|[4-9][0-9])|CXXABI_1\.3\.(1[4-9]|[2-9][0-9]))/ {print FILENAME \": \" \$0}"' _ {} \;)"
if [ -n "${BAD_SYMS}" ]; then
    echo "❌ Remaining newer glibc/GLIBCXX symbols in AppDir:"
    echo "${BAD_SYMS}"
    exit 1
fi
echo "✅ Zero GLIBC > 2.35 or GLIBCXX > 3.4.30 references in AppDir."

echo "==> Preparing AppDir metadata..."
cp "${WORKSPACE_DIR}/packaging/appimage/AppRun" "${APP_DIR}/AppRun"
chmod +x "${APP_DIR}/AppRun"

cp "${WORKSPACE_DIR}/assets/zalos.desktop" "${APP_DIR}/zalos.desktop"
cp "${WORKSPACE_DIR}/assets/icons/zalos.svg" "${APP_DIR}/zalos.svg"
mkdir -p "${APP_DIR}/usr/share/metainfo"
rm -f "${APP_DIR}/usr/share/metainfo/"*.xml
cp "${WORKSPACE_DIR}/assets/metainfo/io.github.cpfriend1721994.zalos.metainfo.xml" \
    "${APP_DIR}/usr/share/metainfo/io.github.cpfriend1721994.zalos.appdata.xml"

if command -v rsvg-convert &>/dev/null; then
    rsvg-convert -w 256 -h 256 "${WORKSPACE_DIR}/assets/icons/zalos.svg" -o "${APP_DIR}/zalos.png"
    cp "${APP_DIR}/zalos.png" "${APP_DIR}/.DirIcon"
fi

mkdir -p /tmp/tools
TOOL="/tmp/tools/appimagetool-${ARCH}"
if [ ! -f "${TOOL}" ]; then
    echo "==> Downloading appimagetool (${ARCH})..."
    if ! curl -sSfL --retry 5 --retry-delay 3 --retry-all-errors -o "${TOOL}" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage"; then
        echo "==> appimagetool download failed (no network access to github.com from this environment)"
        rm -f "${TOOL}"
    fi
    [ -f "${TOOL}" ] && chmod +x "${TOOL}" || true
fi

rm -f "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage" "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage.zsync" "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}-AppDir.tar.gz"
BUILT=0
LOG="/tmp/tools/appimagetool-${ARCH}.log"
: > "${LOG}"
if [ -f "${TOOL}" ] && [ -x "${TOOL}" ]; then
    RUNTIME_FILE="/tmp/tools/runtime-${ARCH}"
    if [ ! -f "${RUNTIME_FILE}" ]; then
        OFFSET=$("${TOOL}" --appimage-offset 2>/dev/null || true)
        if [ -n "${OFFSET}" ] && [ "${OFFSET}" -gt 0 ] 2>/dev/null; then
            head -c "${OFFSET}" "${TOOL}" > "${RUNTIME_FILE}" || rm -f "${RUNTIME_FILE}"
        fi
    fi

    UPDATE_INFO="gh-releases-zsync|cpfriend1721994|zalos|latest|Zalos-*-${ARCH}.AppImage.zsync"
    RUNTIME_ARGS=(-n -u "${UPDATE_INFO}" --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 --mksquashfs-opt -b --mksquashfs-opt 1M)
    [ -s "${RUNTIME_FILE}" ] && RUNTIME_ARGS+=(--runtime-file "${RUNTIME_FILE}")

    echo "==> Generating AppImage via appimagetool (zstd -22 + 1M blocks)..."
    if ARCH="${ARCH}" "${TOOL}" --appimage-extract-and-run "${RUNTIME_ARGS[@]}" "${APP_DIR}" "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage" >>"${LOG}" 2>&1; then
        BUILT=1
    else
        (
            cd /tmp/tools
            rm -rf "squashfs-root-${ARCH}"
            "${TOOL}" --appimage-extract >/dev/null 2>&1 || true
            [ -d squashfs-root ] && mv squashfs-root "squashfs-root-${ARCH}"
        ) >>"${LOG}" 2>&1 || true
        if [ -x "/tmp/tools/squashfs-root-${ARCH}/AppRun" ]; then
            ARCH="${ARCH}" "/tmp/tools/squashfs-root-${ARCH}/AppRun" "${RUNTIME_ARGS[@]}" "${APP_DIR}" "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage" >>"${LOG}" 2>&1 && BUILT=1 || echo "==> appimagetool execution failed in this environment."
        fi
    fi
fi

if [ "${BUILT}" = "1" ] && [ -f "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage" ]; then
    ZSYNC_NAME="Zalos-${VERSION}-${ARCH}.AppImage.zsync"
    [ -f "${ZSYNC_NAME}" ] && mv -f "${ZSYNC_NAME}" "${OUTPUT_DIR}/"
    ls -lh "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage"*
    echo "✅ Generated ${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}.AppImage"
else
    echo "==> appimagetool could not produce an AppImage; last lines of ${LOG}:"
    tail -n 15 "${LOG}" 2>/dev/null || true
    echo "==> Packaging portable AppDir bundle (the CI host wraps it into .AppImage)..."
    tar -czf "${OUTPUT_DIR}/Zalos-${VERSION}-${ARCH}-AppDir.tar.gz" -C "${APP_DIR}" .
fi

echo "✅ AppImage build step completed. Artifacts saved in dist/"
