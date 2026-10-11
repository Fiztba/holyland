#!/usr/bin/env bash
# =============================================================================
# tools/setup.sh — one-shot, idempotent host setup.
#
#   1. Python venv at .venv with tools/requirements.txt.
#   2. Period compilers from decompme/compilers into tools/cc/:
#        ee-gcc 2.9-ee-991111-01  compiles everything; its ee-as 2.9 assembles
#                                 the game, libc, libm and libgcc
#        ee-gcc 2.96-ee-001003-1  only its SCE ee-as 2.10, for the SDK archives
#   3. GNU ld 2.10 (+ R5900/DVP patches) and dvp-as, built from public source.
#
# KFIV's SDK archives are the same releases ICO links (libkernl/libcdvd/libmc
# 2.2.4, libgraph/libdma/libpad/libipu/libmpeg 2.2.0), and ICO's compiled
# library sources byte-match KFIV, so this is ICO's toolchain unchanged. The
# two binutils patches and the obstack shim come from the ico decompilation
# (github.com/nathanialf/ico, MIT).
#
# Nothing downloaded here is game data.  SKIP_TOOLCHAIN=1 skips steps 2-3.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PYTHON="${PYTHON:-python3}"
VENV="${VENV:-.venv}"
CC_DIR="$ROOT/tools/cc"
CCSRC="$CC_DIR/src"
# binutils' autoconf trees and LD_PRELOAD (tools/period_env.sh) both break on
# paths with spaces.
case "$ROOT" in *" "*) echo "==> the checkout path must not contain spaces: $ROOT" >&2; exit 1 ;; esac

echo "==> kfiv setup at $ROOT"

# --- 1. Python venv ----------------------------------------------------------
if [[ ! -d "$VENV" ]]; then
    echo "==> creating venv at $VENV"
    "$PYTHON" -m venv "$VENV"
fi
"$VENV/bin/python" -m pip install -q --upgrade pip
"$VENV/bin/python" -m pip install -q -r tools/requirements.txt

[[ "${SKIP_TOOLCHAIN:-0}" == "1" ]] && { echo "==> SKIP_TOOLCHAIN=1; done"; exit 0; }

# --- 2. Period compilers -----------------------------------------------------
DECOMPME="https://github.com/decompme/compilers/releases/download/compilers"
fetch_compiler() {
    local label="$1" dir="$2" url="$3" sentinel="$4"
    if [[ -f "$sentinel" ]]; then
        echo "==> $label already at $dir"
        return 0
    fi
    echo "==> fetching $label"
    mkdir -p "$dir"
    curl -fsSL "$url" | tar -xJ -C "$dir" --strip-components=1 ||
        { rm -rf "$dir"; echo "==> $label fetch failed" >&2; return 1; }
}
fetch_compiler "ee-gcc 2.9-991111-01" "$CC_DIR/ee-gcc2.9-991111" \
    "$DECOMPME/ee-gcc2.9-991111-01.tar.xz" "$CC_DIR/ee-gcc2.9-991111/ee-gcc"
fetch_compiler "ee-gcc 2.96 (for SCE ee-as 2.10)" "$CC_DIR/ee-gcc2.96" \
    "$DECOMPME/ee-gcc2.96.tar.xz" "$CC_DIR/ee-gcc2.96/bin/as"

if ! "$CC_DIR/ee-gcc2.9-991111/ee-gcc" --version >/dev/null 2>&1; then
    cat >&2 <<'EOF'
==> ee-gcc is a 32-bit i386 binary and does not run on this host.
    Install 32-bit glibc (Fedora: sudo dnf install glibc.i686 libstdc++.i686;
    Debian: libc6:i386 libstdc++6:i386) and re-run.
EOF
fi

# --- 3. GNU ld 2.10 + dvp-as -------------------------------------------------
BU210_URL="https://ftp.gnu.org/gnu/binutils/binutils-2.10.tar.gz"
BU210_SHA256="fd7d227c0dd15cf5448385e56b8ad8313cd491839834b57c0c086ac7b7819a15"
BU210_PATCHES=("$ROOT/tools/binutils-2.10-ee.patch" "$ROOT/tools/binutils-2.10-dvp-ld.patch")
BU210_DIR="$CC_DIR/binutils-2.10-ee"
DVP_REPO="https://github.com/ps2dev/binutils-gdb"
DVP_COMMIT="3eb45ea37f0efd498d1de3cf9562de07197aefa8"   # dvp-v2.45.1
DVP_DIR="$CC_DIR/dvp-as"

apply_patch() {  # host may lack patch(1); git apply works outside a repo
    if command -v patch >/dev/null 2>&1; then patch -s -p1 < "$1"; else git apply -p1 "$1"; fi
}

build_dvp_as() {
    local stamp="$DVP_DIR/.stamp"
    if [[ -x "$DVP_DIR/bin/dvp-as" && "$(cat "$stamp" 2>/dev/null)" == "$DVP_COMMIT" ]]; then
        echo "==> dvp-as already at $DVP_DIR"; return 0
    fi
    echo "==> fetching ps2dev binutils-gdb $DVP_COMMIT"
    rm -rf "$CCSRC/dvp-binutils" "$CCSRC/build-dvp"
    mkdir -p "$CCSRC/dvp-binutils" "$CCSRC/build-dvp" "$DVP_DIR/bin"
    git -C "$CCSRC/dvp-binutils" init -q
    git -C "$CCSRC/dvp-binutils" fetch -q --depth 1 "$DVP_REPO" "$DVP_COMMIT"
    git -C "$CCSRC/dvp-binutils" checkout -q FETCH_HEAD
    echo "==> building dvp-as"
    ( cd "$CCSRC/build-dvp" &&
      ../dvp-binutils/configure --target=dvp --prefix="$DVP_DIR" \
          --disable-nls --disable-werror --disable-gdb --disable-gdbserver \
          --disable-sim --disable-gprof --disable-gprofng --disable-libdecnumber \
          --disable-readline --without-zstd > configure.log 2>&1 &&
      make -j"$(nproc)" all-gas MAKEINFO=true > make.log 2>&1 ) ||
        { echo "==> dvp-as build failed; see $CCSRC/build-dvp/*.log" >&2; return 1; }
    cp "$CCSRC/build-dvp/gas/as-new" "$DVP_DIR/bin/dvp-as"
    echo "$DVP_COMMIT" > "$stamp"
}

build_ld210() {
    local stamp="$BU210_DIR/.stamp" want
    want="$BU210_SHA256 $(cat "${BU210_PATCHES[@]}" | sha256sum | cut -d' ' -f1)"
    if [[ -x "$BU210_DIR/bin/ld" && "$(cat "$stamp" 2>/dev/null)" == "$want" ]]; then
        echo "==> ld 2.10 already at $BU210_DIR"; return 0
    fi
    mkdir -p "$CCSRC" "$BU210_DIR/bin"
    local tgz="$CCSRC/binutils-2.10.tar.gz"
    if [[ ! -f "$tgz" ]] || ! echo "$BU210_SHA256  $tgz" | sha256sum -c --status; then
        echo "==> fetching GNU binutils 2.10"
        curl -fsSL -o "$tgz" "$BU210_URL"
        echo "$BU210_SHA256  $tgz" | sha256sum -c --status ||
            { echo "==> binutils-2.10.tar.gz sha256 mismatch" >&2; return 1; }
    fi
    echo "==> building ld 2.10"
    rm -rf "$CCSRC/binutils-2.10" "$CCSRC/build-ld210"
    tar -xzf "$tgz" -C "$CCSRC"
    ( cd "$CCSRC/binutils-2.10" && for p in "${BU210_PATCHES[@]}"; do apply_patch "$p"; done ) || return 1
    cp "$CCSRC/dvp-binutils/config.sub" "$CCSRC/dvp-binutils/config.guess" "$CCSRC/binutils-2.10/"
    mkdir -p "$CCSRC/build-ld210"
    ( cd "$CCSRC/build-ld210" &&
      ac_cv_prog_lex_root=lex.yy ac_cv_prog_LEX=flex ac_cv_prog_YACC="bison -y" LEXLIB= \
      CC="gcc -std=gnu89 -fcommon -w -g -O1" \
          ../binutils-2.10/configure --target=mipsel-elf --disable-nls > configure.log 2>&1 &&
      make -j"$(nproc)" all-ld MAKEINFO=true > make.log 2>&1 ) ||
        { echo "==> ld 2.10 build failed; see $CCSRC/build-ld210/*.log" >&2; return 1; }
    cp "$CCSRC/build-ld210/ld/ld-new" "$BU210_DIR/bin/ld"
    echo "$want" > "$stamp"
}

# dvp-as first: the 2.10 build borrows its config.sub/config.guess.
build_dvp_as || exit 1
build_ld210 || exit 1

# objcopy for the ROM view: any MIPS-capable host objcopy will do.
if ! command -v mips-linux-gnu-objcopy >/dev/null && ! command -v mips64-linux-gnu-objcopy >/dev/null; then
    echo "==> warning: no mips(64)-linux-gnu-objcopy on PATH (Fedora: binutils-mips64-linux-gnu)" >&2
fi

echo "==> setup complete"
