#!/usr/bin/env bash
set -euo pipefail
cd /home/tarcila/Code/ANARI/vela/broad-editing-cuda
base="$PWD/.scratch/reliability-verification/orbit-runs"
mkdir -p "$base"
run=$(mktemp -d "$base/run-XXXXXX")
export HOME="$run/home" XDG_CONFIG_HOME="$run/home/.config"
export XDG_RUNTIME_DIR="$run/runtime" TMPDIR="$run/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
export ANARI_LIBRARY=helide ANARI_VSR_LIBRARY=helide VSR_ANARI_LIBRARIES=helide
export VSR_LUA_PACKAGE_PATHS=''
export LD_LIBRARY_PATH="$PWD/_out/_cuda-install/lib:$PWD/_out/_cuda-check:${LD_LIBRARY_PATH:-}"
status=0
timeout 90s _out/_cuda-check/vsrTests '*follows a client orbit exactly*' > "$run/output.log" 2>&1 || status=$?
printf 'run=%s exit=%s\n' "$run" "$status"
if [ "$status" -ne 0 ]; then
  grep -A 6 -B 2 'FAILED:' "$run/output.log" || true
fi
exit "$status"
