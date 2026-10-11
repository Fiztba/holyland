#!/usr/bin/env bash
# Ghidra MCP server for KFIV (pyghidra-mcp, headless).
#
#   tools/ghidra_mcp.sh import   import build/kfiv.elf into the project and analyze it
#   tools/ghidra_mcp.sh          serve the project over MCP stdio (what Claude Code runs)
#
# One-time setup:
#   uv tool install --python 3.13 pyghidra-mcp
#   Ghidra 12.x + JDK 21, with ghidra-emotionengine-reloaded unzipped into
#   $GHIDRA_INSTALL_DIR/Ghidra/Extensions/
#
# Overrides: GHIDRA_INSTALL_DIR, JAVA_HOME, KFIV_GHIDRA_PROJECT (a .gpr path).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
export GHIDRA_INSTALL_DIR=${GHIDRA_INSTALL_DIR:-$HOME/opt/ghidra_12.1.3_PUBLIC}
if [[ -z ${JAVA_HOME:-} || ! -x $JAVA_HOME/bin/java ]] ||
   ! "$JAVA_HOME/bin/java" -version 2>&1 | grep -q '"2[1-9]'; then
    for j in "$HOME"/.sdkman/candidates/java/21* /usr/lib/jvm/java-21*; do
        [[ -x $j/bin/java ]] && { export JAVA_HOME=$j; break; }
    done
fi
PROJECT=${KFIV_GHIDRA_PROJECT:-$HOME/ghidra-projects/kfiv/kfiv.gpr}
TOOL_PY=$(uv tool dir)/pyghidra-mcp/bin/python

case ${1:-serve} in
import)
    exec "$TOOL_PY" -I "$ROOT/tools/ghidra_import.py" --project "$PROJECT"
    ;;
serve)
    if [[ ! -f $PROJECT ]]; then
        echo "no Ghidra project at $PROJECT; run tools/ghidra_mcp.sh import" >&2
        exit 1
    fi
    shift || true
    exec "$(uv tool dir --bin)/pyghidra-mcp" --project-path "$PROJECT" "$@"
    ;;
*)
    echo "usage: $0 [import|serve [pyghidra-mcp args...]]" >&2
    exit 2
    ;;
esac
