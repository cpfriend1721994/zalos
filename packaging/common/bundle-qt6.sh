#!/usr/bin/env bash
set -eo pipefail

QT_ROOT="${1:?set qt_root}"
DEST_QT="${2:?set dest_qt_dir}"
BIN_DIR="${3:?set bin_dir}"
LIBEXEC_DIR="${4:?set libexec_dir}"
BUILD_ICU_SHIM="0"
if [ "${5:-}" = "--build-icu-shim" ]; then
    BUILD_ICU_SHIM="1"
fi

QT_PLUG_SRC="${QT_ROOT}/plugins"
QT_QML_SRC="${QT_ROOT}/qml"
QT_LIB_SRC="${QT_ROOT}/lib"

rm -rf "${DEST_QT}"
mkdir -p "${DEST_QT}/lib" "${DEST_QT}/plugins" "${DEST_QT}/qml"

copy_plugin() {
    local sub="$1" file="$2"
    if [ -f "${QT_PLUG_SRC}/${sub}/${file}" ]; then
        mkdir -p "${DEST_QT}/plugins/${sub}"
        cp -L "${QT_PLUG_SRC}/${sub}/${file}" "${DEST_QT}/plugins/${sub}/"
    fi
}

for p in libqxcb.so libqoffscreen.so libqwayland-generic.so libqwayland-egl.so libqwayland.so; do
    copy_plugin platforms "$p"
done
for d in xcbglintegrations wayland-shell-integration wayland-graphics-integration-client platforminputcontexts; do
    if [ -d "${QT_PLUG_SRC}/${d}" ]; then
        mkdir -p "${DEST_QT}/plugins/${d}"
        find "${QT_PLUG_SRC}/${d}" -maxdepth 1 -name '*.so' -exec cp -L {} "${DEST_QT}/plugins/${d}/" \;
    fi
done
copy_plugin sqldrivers libqsqlite.so
copy_plugin tls libqopensslbackend.so
copy_plugin tls libqcertonlybackend.so
for p in libqjpeg.so libqgif.so libqwebp.so libqsvg.so libqico.so; do
    copy_plugin imageformats "$p"
done
copy_plugin iconengines libqsvgicon.so
if [ -f "${QT_PLUG_SRC}/multimedia/libgstreamermediaplugin.so" ]; then
    copy_plugin multimedia libgstreamermediaplugin.so
elif [ -f "${QT_PLUG_SRC}/multimedia/libffmpegmediaplugin.so" ]; then
    copy_plugin multimedia libffmpegmediaplugin.so
fi

for mod in QtCore QtQml QtMultimedia; do
    [ -d "${QT_QML_SRC}/${mod}" ] && cp -a "${QT_QML_SRC}/${mod}" "${DEST_QT}/qml/"
done
mkdir -p "${DEST_QT}/qml/QtQuick/Controls"
for f in qmldir libqtquick2plugin.so; do
    [ -f "${QT_QML_SRC}/QtQuick/${f}" ] && cp -L "${QT_QML_SRC}/QtQuick/${f}" "${DEST_QT}/qml/QtQuick/"
done
for sub in Layouts Window Templates Shapes Effects; do
    [ -d "${QT_QML_SRC}/QtQuick/${sub}" ] && cp -a "${QT_QML_SRC}/QtQuick/${sub}" "${DEST_QT}/qml/QtQuick/"
done
for f in qmldir libqtquickcontrols2plugin.so; do
    [ -f "${QT_QML_SRC}/QtQuick/Controls/${f}" ] && cp -L "${QT_QML_SRC}/QtQuick/Controls/${f}" "${DEST_QT}/qml/QtQuick/Controls/"
done
for sub in Basic impl; do
    [ -d "${QT_QML_SRC}/QtQuick/Controls/${sub}" ] && cp -a "${QT_QML_SRC}/QtQuick/Controls/${sub}" "${DEST_QT}/qml/QtQuick/Controls/"
done
find "${DEST_QT}/qml" -type d -name designer -prune -exec rm -rf {} +
find "${DEST_QT}/qml" -type f \( -name '*.qmltypes' -o -name '*.a' -o -name '*.prl' \) -delete

is_excluded_lib() {
    case "$1" in
        linux-vdso.so*|ld-linux-*.so*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|\
        libresolv.so*|libutil.so*|libnsl.so*|libanl.so*|libcrypt.so*|libstdc++.so*|libgcc_s.so*|\
        libGL.so*|libGLX.so*|libGLdispatch.so*|libOpenGL.so*|libEGL.so*|libvulkan.so*|libdrm*.so*|libgbm.so*|libwayland-*.so*|\
        libX11.so*|libX11-xcb.so*|libXau.so*|libXdmcp.so*|libXext.so*|libXfixes.so*|libXrender.so*|libXrandr.so*|\
        libXi.so*|libXcursor.so*|libXinerama.so*|libICE.so*|libSM.so*|\
        libxcb.so*|libxcb-dri*.so*|libxcb-present.so*|libxcb-glx.so*|libxcb-xkb.so*|\
        libglib-2.0.so*|libgobject-2.0.so*|libgio-2.0.so*|libgmodule-2.0.so*|libdbus-1.so*|libsystemd.so*|libudev.so*|\
        libcap.so*|libuuid.so*|libselinux.so*|libmount.so*|libblkid.so*|libapparmor.so*|libffi.so*|libpcre2-8.so*|\
        libfontconfig.so*|libfreetype.so*|libexpat.so*|libfribidi.so*|\
        libz.so*|libzstd.so*|liblzma.so*|liblz4.so*|libbz2.so*|libbrotli*.so*|libpng16.so*|libxkbcommon.so*|libxkbcommon-x11.so*|\
        libgssapi_krb5.so*|libkrb5*.so*|libk5crypto.so*|libcom_err.so*|libkeyutils.so*|libssl.so*|libcrypto.so*|\
        libpulse*.so*|libasound.so*|libsndfile.so*|libFLAC.so*|libvorbis*.so*|libogg.so*|libopus.so*|\
        libmp3lame.so*|libmpg123.so*|libasyncns.so*|libpipewire-*.so*|libjack*.so*|libgst*.so*|liborc-*.so*|\
        libproxy.so*|libpxbackend-*.so*|libcurl*.so*|libduktape.so*|libunistring.so*|\
        libidn2.so*|libpsl.so*|libssh.so*|librtmp.so*|libnghttp2.so*|libldap*.so*|liblber*.so*|\
        libsasl2.so*|libgnutls.so*|libnettle.so*|libhogweed.so*|libtasn1.so*|libp11-kit.so*|\
        libgcrypt.so*|libgpg-error.so*|libgmp.so*|libbsd.so*|libmd.so*|libzalos-glibc-compat.so)
            return 0 ;;
        libicu*.so*)
            if [ "${BUILD_ICU_SHIM}" = "1" ]; then return 0; else return 1; fi ;;
        *)
            return 1 ;;
    esac
}

collect_deps_pass() {
    local added=0
    local roots=("${BIN_DIR}/zalos" "${LIBEXEC_DIR}/"*)
    while IFS= read -r -d '' f; do
        roots+=("$f")
    done < <(find "${DEST_QT}/plugins" "${DEST_QT}/qml" "${DEST_QT}/lib" -type f -name '*.so*' -print0 2>/dev/null)

    while read -r name path; do
        [ -n "${name}" ] || continue
        if is_excluded_lib "${name}"; then
            continue
        fi
        local resolved=""
        if [ -f "${QT_LIB_SRC}/${name}" ]; then
            resolved="${QT_LIB_SRC}/${name}"
        elif [ -f "${path}" ]; then
            resolved="${path}"
        fi
        if [ -n "${resolved}" ] && [ ! -e "${DEST_QT}/lib/${name}" ]; then
            cp -L "${resolved}" "${DEST_QT}/lib/${name}"
            added=1
        fi
    done < <(LD_LIBRARY_PATH="${QT_LIB_SRC}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" ldd "${roots[@]}" 2>/dev/null | awk '/=> \// {print $1, $3}' | sort -u)
    return "${added}"
}

while true; do
    if collect_deps_pass; then
        break
    fi
done

SHIM_TMP="$(mktemp -d)"
trap 'rm -rf "${SHIM_TMP}"' EXIT

if [ "${BUILD_ICU_SHIM}" = "1" ]; then
    QT_CORE_BUNDLED="${DEST_QT}/lib/libQt6Core.so.6"
    ICU_SONAMES="$(readelf -d "${QT_CORE_BUNDLED}" | awk -F'[][]' '/NEEDED.*libicu/ {print $2}')"
    ICU_VER="$(echo "${ICU_SONAMES}" | grep -oE '\.so\.[0-9]+' | grep -oE '[0-9]+' | head -n1)"
    rm -f "${DEST_QT}/lib/libicu"*.so*
    if [ -n "${ICU_VER}" ]; then
        echo "==> Building dynamic host-ICU forwarding shims for ICU ABI .${ICU_VER}..."
        python3 - "${QT_CORE_BUNDLED}" "${QT_LIB_SRC}" "${ICU_VER}" "${SHIM_TMP}" <<'PY'
import glob, os, re, subprocess, sys

qt_core, qt_lib, ver, out_dir = sys.argv[1:5]
und_out = subprocess.check_output(["objdump", "-T", qt_core], text=True)
syms = sorted({
    m.group(1)
    for line in und_out.splitlines()
    if "*UND*" in line
    for m in [re.search(rf"\b([A-Za-z0-9_]+_{re.escape(ver)})$", line)]
    if m
})

def exported(so_name):
    cands = [os.path.join(qt_lib, so_name)]
    cands += glob.glob(f"/usr/lib/*/{so_name}")
    cands += glob.glob(f"/lib/*/{so_name}")
    cands += [f"/usr/lib64/{so_name}", f"/lib64/{so_name}", f"/usr/lib/{so_name}", f"/lib/{so_name}"]
    for p in cands:
        if os.path.exists(p):
            out = subprocess.check_output(["nm", "-D", "--defined-only", p], text=True)
            return {l.split()[-1] for l in out.splitlines() if l.strip()}
    return set()

uc_exports = exported(f"libicuuc.so.{ver}")
i18n_exports = exported(f"libicui18n.so.{ver}")
uc_syms = [s for s in syms if s in uc_exports]
i18n_syms = [s for s in syms if s in i18n_exports and s not in uc_exports]

header = r"""#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void *g_uc = NULL;
static void *g_i18n = NULL;
static int g_host_ver = 0;

static const char *const kHostDirs[] = {
#if defined(__aarch64__)
    "/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu",
#elif defined(__x86_64__)
    "/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu",
#endif
    "/usr/lib64", "/lib64", "/usr/lib", "/lib"
};

static void icu_init(void) {
    char p0[256], p1[256], p2[256];
    for (int v = 90; v >= 58; --v) {
        for (size_t i = 0; i < sizeof(kHostDirs) / sizeof(kHostDirs[0]); ++i) {
            snprintf(p0, sizeof(p0), "%s/libicudata.so.%d", kHostDirs[i], v);
            void *data = dlopen(p0, RTLD_NOW | RTLD_GLOBAL);
            snprintf(p1, sizeof(p1), "%s/libicuuc.so.%d", kHostDirs[i], v);
            void *uc = dlopen(p1, RTLD_NOW | RTLD_GLOBAL);
            if (!uc) {
                if (data) dlclose(data);
                continue;
            }
            snprintf(p2, sizeof(p2), "%s/libicui18n.so.%d", kHostDirs[i], v);
            void *i18n = dlopen(p2, RTLD_NOW | RTLD_GLOBAL);
            if (i18n) {
                g_uc = uc;
                g_i18n = i18n;
                g_host_ver = v;
                return;
            }
            dlclose(uc);
            if (data) dlclose(data);
        }
    }
    for (int v = 90; v >= 58; --v) {
        if (v == ICU_SHIM_VER) continue;
        snprintf(p0, sizeof(p0), "libicudata.so.%d", v);
        void *data = dlopen(p0, RTLD_NOW | RTLD_GLOBAL);
        snprintf(p1, sizeof(p1), "libicuuc.so.%d", v);
        void *uc = dlopen(p1, RTLD_NOW | RTLD_GLOBAL);
        if (!uc) {
            if (data) dlclose(data);
            continue;
        }
        snprintf(p2, sizeof(p2), "libicui18n.so.%d", v);
        void *i18n = dlopen(p2, RTLD_NOW | RTLD_GLOBAL);
        if (i18n) {
            g_uc = uc;
            g_i18n = i18n;
            g_host_ver = v;
            return;
        }
        dlclose(uc);
        if (data) dlclose(data);
    }
}

static void *resolve_sym(void **handle, const char *base) {
    pthread_once(&g_once, icu_init);
    if (!*handle || g_host_ver <= 0) return NULL;
    char name[256];
    snprintf(name, sizeof(name), "%s_%d", base, g_host_ver);
    return dlsym(*handle, name);
}
""".replace("ICU_SHIM_VER", ver)

def emit_c(path, sym_list, handle_expr):
    lines = [header]
    suffix = f"_{ver}"
    for s in sym_list:
        base = s[:-len(suffix)]
        if base == "ucal_setMillis":
            lines.append(f"""void {s}(void *cal, double dt, int32_t *st) {{
    typedef void (*fn_t)(void *, double, int32_t *);
    fn_t fn = (fn_t)resolve_sym(&{handle_expr}, "{base}");
    if (fn) {{ fn(cal, dt, st); return; }}
    if (st && *st <= 0) *st = 16;
}}""")
        else:
            lines.append(f"""uintptr_t {s}(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                         uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                         uintptr_t a8, uintptr_t a9) {{
    typedef uintptr_t (*fn_t)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                              uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
    fn_t fn = (fn_t)resolve_sym(&{handle_expr}, "{base}");
    if (fn) return fn(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    return 0;
}}""")
    with open(path, "w") as fp:
        fp.write("\n".join(lines) + "\n")

emit_c(os.path.join(out_dir, "icuuc_shim.c"), uc_syms, "g_uc")
emit_c(os.path.join(out_dir, "icui18n_shim.c"), i18n_syms, "g_i18n")
print(f"Generated {len(uc_syms)} libicuuc + {len(i18n_syms)} libicui18n shims")
PY

        if [ -s "${SHIM_TMP}/icuuc_shim.c" ] && grep -qE '_[0-9]+\(' "${SHIM_TMP}/icuuc_shim.c"; then
            gcc -O2 -shared -fPIC "${SHIM_TMP}/icuuc_shim.c" \
                -Wl,-soname,"libicuuc.so.${ICU_VER}" -ldl -lpthread -o "${DEST_QT}/lib/libicuuc.so.${ICU_VER}"
            gcc -O2 -shared -fPIC "${SHIM_TMP}/icui18n_shim.c" \
                -Wl,-soname,"libicui18n.so.${ICU_VER}" -ldl -lpthread -o "${DEST_QT}/lib/libicui18n.so.${ICU_VER}"
            gcc -O2 -shared -fPIC -x c /dev/null \
                -Wl,-soname,"libicudata.so.${ICU_VER}" -o "${DEST_QT}/lib/libicudata.so.${ICU_VER}"
        else
            echo "⚠️ No ICU symbols matched for shim generation; copying Qt ICU libs directly."
            cp -L "${QT_LIB_SRC}/libicu"*.so* "${DEST_QT}/lib/" 2>/dev/null || true
        fi
    fi
fi

if ldd "${DEST_QT}/lib/libQt6Network.so.6" 2>/dev/null | grep -q "libproxy.so.1"; then
    cat > "${SHIM_TMP}/proxy_shim.c" <<'EOF'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void *g_lib = NULL;
static void *(*real_new)(void) = NULL;
static char **(*real_get)(void *, const char *) = NULL;
static void (*real_free)(void *) = NULL;
static void (*real_free_proxies)(char **) = NULL;

static void proxy_init(void) {
    static const char *const paths[] = {
#if defined(__aarch64__)
        "/usr/lib/aarch64-linux-gnu/libproxy.so.1", "/lib/aarch64-linux-gnu/libproxy.so.1",
#elif defined(__x86_64__)
        "/usr/lib/x86_64-linux-gnu/libproxy.so.1", "/lib/x86_64-linux-gnu/libproxy.so.1",
#endif
        "/usr/lib64/libproxy.so.1", "/lib64/libproxy.so.1", "/usr/lib/libproxy.so.1"
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        g_lib = dlopen(paths[i], RTLD_LAZY | RTLD_LOCAL);
        if (g_lib) {
            real_new = dlsym(g_lib, "px_proxy_factory_new");
            real_get = dlsym(g_lib, "px_proxy_factory_get_proxies");
            real_free = dlsym(g_lib, "px_proxy_factory_free");
            real_free_proxies = dlsym(g_lib, "px_proxy_factory_free_proxies");
            if (real_new && real_get && real_free) return;
            dlclose(g_lib);
            g_lib = NULL;
        }
    }
}

void *px_proxy_factory_new(void) {
    pthread_once(&g_once, proxy_init);
    if (real_new) return real_new();
    return malloc(1);
}

char **px_proxy_factory_get_proxies(void *pf, const char *url) {
    pthread_once(&g_once, proxy_init);
    if (real_get) return real_get(pf, url);
    const char *env = NULL;
    if (url && strncmp(url, "https://", 8) == 0) env = getenv("https_proxy");
    if (!env && url && strncmp(url, "http://", 7) == 0) env = getenv("http_proxy");
    if (!env) env = getenv("all_proxy");
    if (!env) env = getenv("ALL_PROXY");
    char **res = (char **)calloc(2, sizeof(char *));
    if (!res) return NULL;
    res[0] = strdup((env && *env) ? env : "direct://");
    res[1] = NULL;
    return res;
}

void px_proxy_factory_free_proxies(char **proxies) {
    if (real_free_proxies) { real_free_proxies(proxies); return; }
    if (!proxies) return;
    for (size_t i = 0; proxies[i]; ++i) free(proxies[i]);
    free(proxies);
}

void px_proxy_factory_free(void *pf) {
    if (real_free) { real_free(pf); return; }
    free(pf);
}
EOF
    cat > "${SHIM_TMP}/proxy.map" <<'EOF'
LIBPROXY_0.4.16 {
    global: *;
};
EOF
    gcc -O2 -shared -fPIC "${SHIM_TMP}/proxy_shim.c" \
        -Wl,-soname,libproxy.so.1 -Wl,--version-script,"${SHIM_TMP}/proxy.map" \
        -ldl -lpthread -o "${DEST_QT}/lib/libproxy.so.1"
fi

python3 - "${DEST_QT}" "${BIN_DIR}" "${LIBEXEC_DIR}" <<'PY'
import os, subprocess, sys

dest_qt, bin_dir, libexec_dir = [os.path.abspath(p) for p in sys.argv[1:4]]
qt_lib = os.path.join(dest_qt, "lib")

def is_elf(path):
    if os.path.islink(path) or not os.path.isfile(path):
        return False
    try:
        with open(path, "rb") as fp:
            return fp.read(4) == b"\x7fELF"
    except OSError:
        return False

targets = []
for root_dir in [bin_dir, libexec_dir, dest_qt]:
    if not os.path.isdir(root_dir):
        continue
    for d, _, files in os.walk(root_dir):
        for fn in sorted(files):
            p = os.path.join(d, fn)
            if is_elf(p):
                targets.append(p)

for p in targets:
    bn = os.path.basename(p)
    if bn == "libproxy.so.1" or bn.startswith("libicu"):
        continue
    d = os.path.dirname(p)
    rel_qt = os.path.relpath(qt_lib, d)
    rpath_qt = "$ORIGIN" if rel_qt == "." else "$ORIGIN/" + rel_qt
    subprocess.check_call(["patchelf", "--set-rpath", rpath_qt, p])
    if ".so" in bn:
        subprocess.run(["strip", "--strip-unneeded", "--remove-section=.comment", p], check=False)
PY

if [ -f "${QT_ROOT}/../../Licenses/LICENSE.LGPLv3" ]; then
    cp -L "${QT_ROOT}/../../Licenses/LICENSE.LGPLv3" "${DEST_QT}/LICENSE.Qt-LGPLv3"
elif [ -f "${QT_ROOT}/../Licenses/LICENSE.LGPLv3" ]; then
    cp -L "${QT_ROOT}/../Licenses/LICENSE.LGPLv3" "${DEST_QT}/LICENSE.Qt-LGPLv3"
else
    cat > "${DEST_QT}/LICENSE.Qt-LGPLv3" <<'LIC'
Qt 6 libraries bundled in this directory are licensed under the GNU Lesser General Public License v3 (LGPLv3).
Source code and build instructions: https://download.qt.io/official_releases/qt/6.8/6.8.3/
LIC
fi

echo "==> Bundled Qt 6 into ${DEST_QT} ($(du -sh "${DEST_QT}" | cut -f1))"
