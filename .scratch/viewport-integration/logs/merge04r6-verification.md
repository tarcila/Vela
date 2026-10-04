# Merge04 retry6 — independent acceptance

Workspace: `/home/tarcila/Code/ANARI/vela/viewport-integration`.
Worker: `olyxvkvnrwlporkssokuzkrpxunrlzpy` / `9bc315db604312ac2f98ad0d6aa6141c1ff9a3db`.
Recorded integration base: `704dd0d07545d0380c14e84661dff2d7d02e6c74`.
Spec SHA: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`, independently unchanged.

## History / scope

Initial working copy clean, bookmark exactly recorded base. Owned eleven-revision chain from base to worker, not an immediate-parent claim: worker immediate parent `6b7185971c6e92bb0b620539203e4e5905d9bfec`. `jj new 9bc315db` created clean empty verification child `xunlopkp/3ec6d9fa`. Explicit range `conflicts()` empty and worker `conflict=false`; no rebase or semantic repair needed. Actual range stats, source diffs, resolved ticket/latest comments, c11 outcome/proofs, production client/UI/oracle helpers, complete03 memory/c10 profile proof and prior withheld merger counterexamples inspected. Production code unchanged from `c45155b7` through worker: only public PresentedFrame test changes in src. All source/resume/proof/ticket records retained. No source/spec/ticket/ledger/executor/user-config/controller/other-workspace changes, delegation, rawgit, resets, feature disables or retries until green. Artifact files here are ignored; verification checkout remains clean.

## Environment / actual commands

Every shell cd to workspace above. All runtime used:
`export TMPDIR="$PWD/.scratch/viewport-integration/tmp" LD_LIBRARY_PATH="$PWD/_out/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"`.
Own existing warm Ninja Multi-Config CMAKE_HOME_DIRECTORY/install prefix verified; RelWithDebInfo -O2/-g unchanged, executable debug compression retained; CUDA/UI/interactive/network/Lua/SDL3/integration/MCP enabled. No cold configuration. Commands and actual exits:

- `cmake --build _out/_cmake --config RelWithDebInfo -j3` and `cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`: both0, before runtime. `merge04r6-{build,install}.log`.
- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`:0,68/68,79.63s. `merge04r6-ctest.log`.
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests`:0,355 cases/125484 assertions. `merge04r6-integration.log`.
- Same executable `'[request-guidance],[view-info],[view-identity],[presented-frame],[viewer-presentation],[translation-preview]'`:0,49 cases/8853 assertions,01/02/03/04/05. `merge04r6-focused.log`.
- Same executable `'Viewer output profile and instance gate precede pixel access'`:0,1 case/446 assertions;64 terminal requests then BUSY; expiry drops6698171→5613824B, releases1084347B, natural production/presentation recovers. No count/deadline change. `merge04r6-retention.log`.
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation]'`:0,26 cases/4424 assertions;0 errors/0 definite or indirect loss;27408B possible/181 blocks,32B default suppression, ioctl/address warnings disclosed. `merge04r6-memcheck.log`.
- `jj diff --from 704dd0d0 --to 9bc315db --name-only | grep -E '^src/.*\.(cpp|hpp|h)$' | xargs clang-format --dry-run --Werror`:0,17 C++ files. `merge04r6-format.log`.
- `c++ -std=c++17 -O2 -fPIC -shared .scratch/viewport-integration/04-production-oracle.cpp $(pkg-config --cflags --libs sdl3) -ldl -o "$PWD/.scratch/viewport-integration/logs/04-production-oracle.so"`:0.
- `c++ -std=c++17 -O2 -I src -I external .scratch/viewport-integration/04-production-ui.cpp _out/_cmake/RelWithDebInfo/libvsr_core.a _out/_cmake/RelWithDebInfo/libfmt.a -lpthread -o "$PWD/.scratch/viewport-integration/logs/04-production-ui"`:0.
- Node24 `--check .scratch/viewport-integration/04-production-client.mjs`:0.

## Fixed production plan, all first attempts pass

For each of Barney b1/b2 and Helide h1/h2, serially:
`VSR_PRODUCTION_FRAGMENT_CONTROL=1 VSR_PRODUCTION_LIFECYCLE=1 python .scratch/viewport-integration/04-production-viewer.py DEVICE production-merge04r6-TAG`.
All4 exits0. Actual own rebuilt production vsrViewer, native PiToolDefinition/client/proxy/HTTP images, no test-source Meta.Tree.Frame. Each31 parity PNGs/1 cold/0 busy retries plus1 fresh PNG after genuine UI close/reopen. Total124 parity PNGs/4 fresh lifecycle PNGs/368000 independent sampled pixels. Strict SDL scale2 rejection/no image; scale1 naturally presented recovery; matched device/config/production/epoch-in-source-frame identity and observation/submission times; historical resize replay identical; old identity denied/minimal status; current production whole-session READ withdrawal denied. Direct SDL diagnostic pixels never feed MCP. Frame native758x229 and278x139, display geometry remains distinct from fractional renderResolutionScale.

Independent `python logs/merge04r6-audit.py` crosschecks successful actual quad trace serial/command sequence/scale/time against every control receipt, positively observed EOF parser attempts, native operation ordering, fresh identity/frame/config and recovery submission/observation times. Final exit0: `merge04r6-production-audit2.json`. First audit attempt failed KeyError on omitted optional unavailable reason in an available observation; changed only independent audit access to `.get`, preserved error log; not a product counterexample, assertion relaxation or rerun of production. Unavailable result contract omits observation timestamp; causal ordering is awaited successful SDL receipt before native admission plus inspected helper code and native operation sequence, not a fabricated unavailable timestamp. All four runs are independent fresh Xvfb/XDG/controller instances, no user viewer contacted.

## Deterministic old-oracle RED / current GREEN

Independent fixture `logs/merge04r6-fragment-fixture.cpp` includes actual parser from old revision (extracted with `jj file show -r 6b718597`) or current helper. Empty published file calls real `input(nullptr,dir)`; EOF needs no renderer access. Both built `c++ -std=c++17 -O2 -DORACLE_SOURCE=\"ABS_SOURCE\" ... $(pkg-config --cflags --libs sdl3) -ldl`.
Old actual parser exit1: EOF consumed/deleted and acknowledged. New actual parser exit0: incomplete command retained, no acknowledgement. Logs `merge04r6-fragment-{red,green}.log`, binaries/source preserved. Four production runs independently exercise this partial-file negative control through real SDL; then complete atomic control publication is bound to real successful scale2 and scale1 presentation receipts. Old source not edited/rebuilt into the production viewer. Busy regression confirms finite retained terminal jobs, not guessed stuck encoders. Existing caps/retries/deadlines unchanged.

## Actual source/lifetime/pressure reruns

All use private runner `04-live-viewer.py`, own paths above and unique `VSR_VIEWER_TEST_LOG_LABEL=merge04r6` (entropy label `merge04r6-entropy`):

- `VSR_VIEWER_TEST_DESTRUCTIVE=1 python ... barney pressure-baseline`:0,1/577;113 submissions/124 heartbeats, observer disabled.
- Same destructive `barney pressure-probe`:0,1/997;34 PNG/14 cold,112 primary/108 secondary submissions/124 heartbeats;primary generation2→3/secondary2 with97 further presentations;0 off-owner/mismatched acquisitions;264078835B peak<268435456B;primary acquire1.32767ms/secondary4.52536ms,1080160 independent pixels.
- Add `VSR_VIEWER_TEST_ENTROPY=1`, same destructive pressure-probe:0,1/934;24 PNG/24 cold,5327939B largest PNG,174310774B held HTTP ownership released;112 primary/108 secondary submissions/123 heartbeats,247860 sampled pixels;cancel/READ/drain protections true. Drain metric booleans remain booleans, not invented lease counts.
- `python ... barney probe`:0,1/738;44PNG/4cold,112 submissions/124 heartbeats,110436 sampled pixels.
- `python ... helide probe`:0,1/734;44PNG/4cold,112 submissions/113 heartbeats,107040 sampled pixels.

Raw runner logs/proofs retained at original distinct paths and copied without deletion to absolute `logs/merge04r6-preserved-*`; wrapper logs all `logs/merge04r6-*-wrapper.log`. This directory is `/home/tarcila/Code/ANARI/vela/viewport-integration/.scratch/viewport-integration/logs`.

## Limits / readiness

Actual production PNG goal verified for instance-bound SDL_CPU_READY_V1 HOST/MANAGED CPU-ready unit-SDL-scale path. Unknown/GPU-only interop/output/effects/non-unit scale remain unsupported, not universal GPU/high-DPI/path claims. Limits unchanged2048axis/2073600pixels/8MiBPNG/12MiBresponse/16MiBproxy/256MiBowned. CPU bank is not whole RSS/VRAM or physical-OOM proof. c10 positive on/off Nsight proof inspected (104 managed allocations each,227/215/217 observer ranges,0 owner CUDA calls/CPU faults/GPU allocation events); production source unchanged, not rerun or claimed new instrumentation. GUI7 external error contexts/same2064B DBus leak remain NOT clean. Prior unsupported sanitizer, Massif timeout and post-exit GDB wrapper failure remain failures. No monitor scanout/OS parity or whole-spec completion.

All independent landing gates passed. Accepted revision is the existing worker9bc315db; fast-forward-equivalent bookmark move preserves its full stack. Ticket06 and final whole-spec independent review remain outer-orchestrator-owned and pending. No blockers to this bounded landing. Cleanup/progress ledger untouched.
