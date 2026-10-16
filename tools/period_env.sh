#!/usr/bin/env bash
# Run a period toolchain binary with tools/period_obstack.c preloaded (see
# that file). Adapted from the ico decompilation (MIT); finds the host's
# 32-bit libc on Debian- and Fedora-style layouts.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SO="${ROOT}/build/period_obstack.so"
case "${SO}" in *" "*) echo "period_env.sh: LD_PRELOAD cannot take a path with spaces: ${SO}" >&2; exit 1 ;; esac
SRC="${ROOT}/tools/period_obstack.c"
if [ ! -f "${SO}" ] || [ "${SRC}" -nt "${SO}" ]; then
    LIBC32=""
    for c in /lib/i386-linux-gnu/libc.so.6 /usr/lib/libc.so.6 /usr/lib32/libc.so.6 /lib/libc.so.6; do
        if [ -f "$c" ] && file -L "$c" | grep -q 'ELF 32-bit'; then LIBC32="$c"; break; fi
    done
    [ -n "${LIBC32}" ] || { echo "period_env.sh: no 32-bit libc.so.6 found" >&2; exit 1; }
    mkdir -p "${ROOT}/build"
    TMP="${SO}.$$"
    gcc -m32 -shared -fPIC -nostdlib -o "${TMP}" "${SRC}" "${LIBC32}" && mv -f "${TMP}" "${SO}" ||
        { rm -f "${TMP}"; echo "period_env.sh: cannot build ${SO}" >&2; exit 1; }
fi
LD_PRELOAD="${SO}${LD_PRELOAD:+:${LD_PRELOAD}}" exec "$@"
