#!/usr/bin/env bash
set -euo pipefail
cd /home/tarcila/Code/ANARI/vela/broad-editing-cuda
base="$PWD/.scratch/reliability-verification/final-runs"
mkdir -p "$base"
run=$(mktemp -d "$base/${1:?mode required}-XXXXXX")
export HOME="$run/home" XDG_CONFIG_HOME="$run/home/.config"
export XDG_RUNTIME_DIR="$run/runtime" TMPDIR="$run/tmp"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
export ANARI_LIBRARY=helide ANARI_VSR_LIBRARY=helide VSR_ANARI_LIBRARIES=helide
export VSR_LUA_PACKAGE_PATHS=''
export LD_LIBRARY_PATH="$PWD/_out/_cuda-install/lib:$PWD/_out/_cuda-check:${LD_LIBRARY_PATH:-}"
printf 'artifacts=%s\n' "$run"
case "$1" in
  full)
    ctest --test-dir _out/_cuda-check -j4 --output-on-failure 2>&1 | tee "$run/output.log"
    grep -q '100% tests passed, 0 tests failed out of 68' "$run/output.log"
    ;;
  full-repeat)
    ctest --test-dir _out/_cuda-check -j4 --output-on-failure \
      --repeat until-fail:3 2>&1 | tee "$run/output.log"
    grep -q '100% tests passed, 0 tests failed out of 68' "$run/output.log"
    ;;
  heavy-repeat)
    ctest --test-dir _out/_cuda-check -j4 --output-on-failure \
      -R '^vsr::(StudioRemote|StudioServer|StudioClient|StudioTestClient)$' \
      --repeat until-fail:10 2>&1 | tee "$run/output.log"
    grep -q '100% tests passed, 0 tests failed out of 4' "$run/output.log"
    ;;
  gpu)
    _out/_cuda-check/vsrTests '[CUDA],[Downsample]' 2>&1 | tee "$run/output.log"
    grep -q 'All tests passed (98792 assertions in 3 test cases)' "$run/output.log"
    compute-sanitizer --tool memcheck --leak-check full --error-exitcode 99 \
      _out/_cuda-check/vsrTests '[CUDA],[Downsample]' 2>&1 | tee "$run/memcheck.log"
    grep -q 'All tests passed (98792 assertions in 3 test cases)' "$run/memcheck.log"
    grep -q 'ERROR SUMMARY: 0 errors' "$run/memcheck.log"
    grep -q 'LEAK SUMMARY: 0 bytes leaked' "$run/memcheck.log"
    ;;
  *) exit 97 ;;
esac
