#!/usr/bin/env bash
# Compile one C source to an object with the period toolchain.
#
#   tools/compile_c.sh src/<path>.c build/src/<path>.c.o
#   tools/compile_c.sh src/<path>.s build/src/<path>.s.o   (hand-written asm:
#                                                          assembler step only)
#
# ee-gcc 2.9-ee-991111-01; the flags and the assembler follow where the code
# came from (the same split ICO's build proves):
#   src/sce/libc|libm|libgcc   compiler-install archives: -O2 -G 0 -fno-builtin, ee-as 2.9
#   src/sce/*                  Sony SDK archives:         -O2 -G 0, SCE ee-as 2.10
#   everything else (the game)                            -O2 -G 8, ee-as 2.9
# The game is -O2 like the SDK: loops with calls (func_00102558, func_00123158,
# func_0019CCB0) match only at -O2. -g changes no game bytes found so far (in
# gcc 2.9 it only stops ee-as filling a branch delay slot after inline asm),
# so it is left off; GAME_CFLAGS overrides the default.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <src.c> <out.o>" >&2; exit 2; }
SRC="$1"; OUT="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC_DIR="${ROOT}/tools/cc/ee-gcc2.9-991111"
AS_OLD="${CC_DIR}/bin/as"
AS_SDK="${ROOT}/tools/cc/ee-gcc2.96/bin/as"

OPT="-O2"
case "${SRC}" in
    src/sce/libc/*|src/sce/libm/*|src/sce/libgcc/*)
        FLAGS="-G 0 -fno-builtin"; GNUM=0; AS="${AS_OLD}" ;;
    src/sce/*)
        FLAGS="-G 0"; GNUM=0; AS="${AS_SDK}" ;;
    *)
        FLAGS="${GAME_CFLAGS:--G 8}"; GNUM=8; AS="${AS_OLD}" ;;
esac

INCS="-I${ROOT}/include"
for d in "${ROOT}"/src/sce/*/; do [ -d "$d" ] && INCS="${INCS} -I${d%/}"; done

mkdir -p "$(dirname "${OUT}")"
case "${SRC}" in
    *.s) exec "${ROOT}/tools/period_env.sh" "${AS}" -EL -mcpu=5900 -mabi=eabi -G "${GNUM}" -o "${OUT}" "${SRC}" ;;
esac
S="${OUT%.o}.s"
# ee-gcc records the path it is given in __FILE__ strings, so compile from the
# source's directory with its bare name.
SRC_DIR="$(cd "$(dirname "${SRC}")" && pwd)"
( cd "${SRC_DIR}" && "${ROOT}/tools/period_env.sh" "${CC_DIR}/ee-gcc" \
    -B "${CC_DIR}/gcc-lib/ee/2.9-ee-991111-01/" -S ${OPT} -mips3 -EL -nostdinc \
    ${FLAGS} ${INCS} -o "${ROOT}/${S}" "$(basename "${SRC}")" )
"${ROOT}/tools/period_env.sh" "${AS}" -EL -mcpu=5900 -mabi=eabi -G "${GNUM}" -o "${OUT}" "${S}"
