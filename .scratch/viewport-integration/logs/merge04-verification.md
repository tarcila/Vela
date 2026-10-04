# Merge04 verification — acceptance withheld

Workspace: /home/tarcila/Code/ANARI/vela/viewport-integration
Worker: xtpustwkyxtlqnmustxopwynmysopxmk / d5bf2b910bb49643c1fe6524630a984ccaf92d7a
Recorded and unchanged integration bookmark: 71812eb67efffd65a450639f9b2db1c19d2140f2
Clean verification working copy: qvsswmrn / 2729de77, empty child of worker. No accepted landing/bookmark move.

## History / scope

Verified all nine owned source/resume revisions form a single chain from recorded base to worker tip. Immediate worker parent is 274df48cbe7f, not the integration base. `jj log -r '71812eb6::d5bf2b91 & conflicts()' --no-graph` returned no conflicts. Inspected actual range summary/stat and source diff (merge04-source.diff), resolved ticket and complete final c10 verification. No new issue records in this range; all existing owned proof/ticket/source commits remain intact. No foreign source or ticket edits, spec/progress/executor mutation, other-workspace edits, rebase, raw git, push or delegation. Spec hash remained e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae.

`jj new d5bf2b910bb49643c1fe6524630a984ccaf92d7a` prepared the clean verification checkout; bookmark intentionally not moved.

## Actual reruns

All shell commands cd to this workspace. Runtime environment:
`export TMPDIR="$PWD/.scratch/viewport-integration/tmp" LD_LIBRARY_PATH="$PWD/_out/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"`

Existing own Ninja Multi-Config tree, RelWithDebInfo, CUDA/UI/interactive apps/network/Lua/SDL3/integration/MCP unchanged enabled. CMAKE_HOME_DIRECTORY and install prefix point here. Existing executable debug compression retained; no cold/reconfiguration or feature disable. Core libraries are static, so install log contains the installed anari_vsr shared library; production viewer runs the rebuilt own build-tree executable.

- `cmake --build _out/_cmake --config RelWithDebInfo -j3` exit0 (merge04-build.log).
- `cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` exit0 (merge04-install.log), before runtime tests.
- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` exit0, 68/68, 75.22s (merge04-ctest.log).
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests` exit0, 355 cases / 125097 assertions (merge04-integration.log).
- Same executable `'[request-guidance],[view-info],[view-identity],[presented-frame],[viewer-presentation],[translation-preview]'` exit0, 49 cases / 8388 assertions (merge04-focused.log). Existing05 tests only; no05 source changes.
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation]'` exit0, 26 cases / 4004 assertions, 0 errors, 0 definite/indirect lost bytes; 28704 possibly lost bytes / 184 blocks, ioctl/large-address warnings disclosed (merge04-memcheck.log).
- `jj diff --from 71812eb6 --to d5bf2b91 --name-only | grep -E '^src/.*\.(cpp|hpp|h)$' | xargs clang-format --dry-run --Werror` exit0, 17 changed C++ files (merge04-format.log).

Private production helper compilation:
- `c++ -std=c++17 -O2 -fPIC -shared .scratch/viewport-integration/04-production-oracle.cpp $(pkg-config --cflags --libs sdl3) -ldl -o .scratch/viewport-integration/logs/04-production-oracle.so` exit0.
- Initial helper link with `-L _out/lib -lvsr_core` failed (static core not installed there), retained merge04-ui-build.log. Correct authoritative worker command: `c++ -std=c++17 -O2 -I src -I external .scratch/viewport-integration/04-production-ui.cpp _out/_cmake/RelWithDebInfo/libvsr_core.a _out/_cmake/RelWithDebInfo/libfmt.a -lpthread -o .scratch/viewport-integration/logs/04-production-ui` exit0 (merge04-ui-build-retry.log).

Isolated authenticated Xvfb/XDG, real own production vsrViewer, unchanged native client:
- `VSR_PRODUCTION_LIFECYCLE=1 python .scratch/viewport-integration/04-production-viewer.py helide merge04-production` exit0: 31 PNGs / 1 cold, 0 busy retries, independent SDL parity, historical resize replay, close/reopen new identity, scale2 rejection and current whole-session READ withdrawal. Proof: 04-helide-merge04-production-proof.json.
- Same command for `barney merge04-production` exit1: client line87 expected scale2 `unavailable`, actual `available`. Failed client log copied to merge04-production-barney-failed-client.log; original wrapper log merge04-production-barney.log. This run is NOT counted as qualified.
- Same command for `barney merge04-production-retry` exit0: 31 PNGs / 1 cold, 0 busy retries, complete lifecycle including unsupported_display_scale_profile, same replay and READ protection. Proof: 04-barney-merge04-production-retry-proof.json.
- Same command for `barney merge04-production-confirm` exit1: native vsr_view_frame McpError busy after bounded helper retry. Failed client log copied to merge04-production-barney-confirm-failed-client.log; wrapper merge04-production-barney-confirm.log. NOT counted as pass.

Actual destructive maximum two-view natural-render tests:
- `VSR_VIEWER_TEST_DESTRUCTIVE=1 python .scratch/viewport-integration/04-live-viewer.py barney pressure-baseline` exit0: 1 case / 580 assertions (merge04-destructive-off*.log).
- Same command `pressure-probe` exit0: 1 case / 1003 assertions, 34 PNGs / 14 cold from48 requests, 113 primary /110 secondary submissions,125 heartbeats; primary generation2→3, secondary2 with98 continuing presentations,0 off-owner starts/0 mismatched acquisitions; shared owned peak264078835B <268435456B; max primary acquire1.45224ms / secondary4.33365ms;1080160 independently compared pixels. No new GPU instrumentation inference from this run (merge04-destructive-on*.log).

## Inspected worker evidence, not rerun instrumentation

Final c10 production proofs: barney and Helide each31 PNGs/1 cold plus actual lifecycle. c10 GPU audit: positive on/off CUDA/OptiX activity,104 managed allocations each;227 start/215 acquire/217 present observer ranges with0 owner CUDA runtime calls/managed CPU faults/GPU allocation events. Complete03 CPU ownership/stack/bookkeeping/held-drain proof referenced by worker. c10 driver audit retains enabled257/disabled261 errors, same7 external contexts and2064B DBus definite leak: NOT clean GUI. Compute Sanitizer unsupported OptiX failure, Massif300s timeout and debugger post-exit wrapper failure remain failures, not passes. These tools were not rerun here.

Supported profile only: instance-bound SDL_CPU_READY_V1 HOST/MANAGED normal completed CPU-ready upload. Unknown interop/storage/output/effects and non-unit SDL/framebuffer scale remain unavailable. Fractional renderResolutionScale is separate from SDL scale. High-DPI parity, universal driver/GPU/RSS bounds, physical monitor parity and whole-spec/06 completion are not claimed.

## Stop / blocker

Acceptance withheld because current integration production Barney verification is intermittent: scale2 expected-unavailable returned available once, and separate confirmation exhausted bounded native busy retry. A full unchanged pass between them does not explain either failure. No source or helper weakening, guessed semantic repair or repeated-until-green acceptance performed. Clean full owned worker checkout and all evidence are preserved; integration bookmark remains at recorded base. Outer orchestrator owns follow-up/cleanup/progress and final independent review.
