# 04 continuation10 — qualified production SDL CPU-ready profile

Outcome **resolved** for ticket04's actual CPU/current-barney presented-frame
contract. This is not whole-spec completion: final independent correctness /
Standards review, fixes and cumulative integration qualification remain the outer
orchestrator's gate. Unsupported interop/storage/effects/non-unit output scales
remain unavailable. No universal GPU-memory, arbitrary-driver, sanitizer, physical
monitor/compositor or sample/quality guarantee is asserted.

## Lineage and preserved scope

Dispatched integration bookmark/base `71812eb67efffd65a450639f9b2db1c19d2140f2`
positively matched `viewport-integration` and ancestors(@) before changes. Parent
`kwzpwpws` / `274df48c` contains source WIP `ozmnozoq` / `8dbdd8df` and the full
owned03/04 series; all were salvaged, not restarted/dropped. Immutable spec SHA
`e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae` unchanged.
Final parent/bookmark/hash checks were repeated before recording/commit. No source
merge/bookmark move, frozen spec/addendum, sibling/historical ticket, ledger,
executor, global manager/cache/encoder/cap/config, other workspace/cache, user's
viewer/controller/Scene/camera/device or remote operation changed.

Every shell cd'ed into this workspace. Own warm RelWithDebInfo j3 build/install;
CUDA/UI/network/Lua/SDL3/integration/MCP stay enabled. CMAKE_HOME_DIRECTORY is this
workspace. Existing `-Wl,--compress-debug-sections=zlib` executable linker flag is
nonsemantic debug compression, preserved without configuration changes. Own
installed `_out/lib` first in lookup, workspace-local TMPDIR and private authenticated
Xvfb/XDG instances throughout. Raw logs/reports/diagnostic images remain ignored;
new proof records explicitly tracked. No subdelegation. Holes/new tickets: none.

## Tight RED → local entry fix → actual destructive GREEN

After own build/install, the exact observer-disabled command
`VSR_VIEWER_TEST_DESTRUCTIVE=1 python .scratch/viewport-integration/04-live-viewer.py barney pressure-baseline`
went **red**, wrapper1 / SIGABRT,60 assertions (59 passed /1 failed).
`04-c10-red-{build,install,baseline-wrapper}.log` records this, complementing the
prior positive GDB Thread2 OptiX/OWL `startFirstFrame(wait=true)` TaskQueue trace
in04-c9. Ranked hypotheses were background eager render/wait, remaining shared
cached Device/Index overlap, then retired callback/index lifetime; candidate tested
as the first variable, not a guessed driver patch/mutex/global-cache redesign.

Removed worker `startFirstFrame(true)` and unused `computeDefaultView()` warmup
(which performed an ANARI_WAIT bounds query). Async preparation remains intact.
`buildUI()` acquire-checks setup publication **before any UI/pass access**. Worker
publishes preparation with release ordering; owning UI activates once and natural
`updateImage -> AnariSceneRenderPass::render` starts asynchronously. Failed setup
stays inactive. No new observation render/wait/cancel/reset/fallback was introduced.
This establishes a narrow host-entry fix for the concrete failure, not a theorem
about all possible driver/cache races. Necessary initial camera bounds behavior
is unchanged; only the unused post-setup bounds warmup was removed.

Exact disabled then enabled destructive loops passed. Regression strengthens the
same real SDL/public seam with owner-thread starts, primary retired/replacement
and secondary device generations, continued secondary presentations, and acquired
render receipts matching the current instance. Actual source teardown/replacement
and disconnection tests fromc9 remain intact. Final two-max-view Nsight run:

- disabled `pressure-baseline-nsys`:1 case/583 assertions,113 submissions/125
  heartbeats, normal managed-host SDL path;
- enabled `pressure-nsys`:1/1005,34 PNGs/14 truthful cold/busy results from48
  requests,23 primary/11 secondary images,112 primary/109 secondary submissions,
  122 heartbeats,1080160 independently compared pixels;
- primary generation2→3, secondary stays2 with97 presentations after retirement;
  off-owner starts0 / mismatched acquisition generations0;
- max native1920x1080 primary and2048x1012 secondary (reported aggregate axis maxima
  2048x1080 are not one over-cap image); retained peak132127636B, charged shared
  peak255086674B (an earlier paired run positively approached264078835B);
- max primary start/acquire/present0.051775/1.32865/0.054487ms, secondary
  0.738825/4.18018/1.24423ms; pin0.067616ms,setup0.150074ms,release1.97073ms,
  each within the100ms applicable qualification bound. Normal SDL upload10.599ms
  and PNG/client delivery261.004ms are **not** observation GPU-readback service
  times, nor sample/quality measures. Normal viewing continues during deferred
  CPU PNG work.

Final `pressure-debug` fixture passed1/1001 and GDB positively reports
`[Inferior 1 ... exited normally]`. The legacy debugger wrapper nevertheless
exits1 because its unconditional post-exit `bt` reports `No stack.` This is a
**debugger wrapper failure**, not a tool-success claim or surviving SIGSEGV.
Positive exact runtime/Nsight loops above supply the automated green verdict.

## Independent maximum GPU attribution + CPU ownership bounds

Final commands (serialize off then on):

`VSR_VIEWER_TEST_DESTRUCTIVE=1 VSR_VIEWER_TEST_NVTX=/nix/store/gy6kn02d2vl127h0h3r8bg1sdi8v72wc-cuda12.8-cuda_nvtx-12.8.90-lib/lib/libnvToolsExt.so.1.0.0 VSR_VIEWER_TEST_NSYS_ARGS='--cuda-memory-usage=true --cuda-um-cpu-page-faults=true --cuda-um-gpu-page-faults=true --cudabacktrace=all:0' python .scratch/viewport-integration/04-live-viewer.py barney pressure-{baseline-nsys,nsys}`

Both runtime/profile commands exit0. `nsys export --type sqlite
--force-overwrite=true --output=.../04-c10-{off,on}.sqlite .../04-pressure-{baseline-nsys,nsys}-barney.nsys-rep`
exit0; `python .scratch/viewport-integration/04-c10-gpu-audit.py` exit0. Exact
proof: `04-c10-gpu-audit.json`; logs `04-c10-final-{off,on}-nsys-wrapper.log`,
`04-pressure-{baseline-nsys,nsys}-barney-viewer.log`, exports/audit under logs/.

Positive trace instrumentation, not missing-table/absent-match inference:

- on:97237 CUDA runtime calls,1628 GPU memory events,104 cudaMallocManaged calls,
  177769 CPU managed faults,3252 GPU managed-fault events,17596 kernels and353174
  copies/migrations;
- off:97928 runtime calls,1636 GPU memory events,104 managed allocations,177924
  CPU faults,3282 GPU faults,17752 kernels and354698 copies/migrations;
- on:227 actual start,215 acquisition and217 checked-presentation callback ranges;
  off:229 owner-thread diagnostic empty start-provider ranges (no acquisition or
  presentation observer). These markers wrap the **same tested ViewerFrameSource**
  callbacks, not normal rendering/SDL transfer. Off-provider returns no snapshot;
- within all on callback ranges:0 owner CUDA runtime calls,0 process GPU allocation
  events,0 CPU managed faults and0 concurrent GPU managed faults. Max acquire
  range4.189002ms. Positive normal synchronization/managed allocation/migration
  records in both captures establish instrument coverage.

Reachable source graph is unchanged03/04: normal pipeline CUDA allocations,
stream completion and normal `SDL_UpdateTexture` first; borrow its already CPU-ready
HOST/MANAGED RGBA bytes only until callback return; bounded metadata+fresh CPU
vector snapshot, checked SDL submission, request-point immutable pin; CPU normalization /
PNG/fixed-stack worker; retained result/status/HTTP leases. Observation has **no
reachable CUDA allocation, transfer, wait/event or driver entry**. The witness
shows no extra managed CPU migration while borrowing normal display-ready bytes.
Thus observation-owned GPU allocations are0 for this qualified source path, not
for the entire viewer or all arbitrary drivers/storage profiles. Normal rendering
and normal display synchronization/migration remain positive and excluded from
observation-only claims; timing/frame counts differ on/off. Nsight's runtime
trace is not a universal inventory of all third-party internal driver calls.

CPU ownership is the full existing [`03-memory-proof.md`](03-memory-proof.md)
allocation/lifetime proof, plus Viewer connection16KiB reservation, production /
output receipt metadata allowances, exact native RGBA vector and all already-owned
leases. All draw from one268435456B pool, including explicit2MiB worker stack,
guard/bookkeeping, service/map/future/callback/control/allocator overhead, retained
PNGs/status/serialization/socket drains and all views. Source callbacks have no
new heap-growth container outside these reservations. Test-only profiler library /
client decoder/oracle allocations are not production observation allocations.
Independent CPU Memcheck and prior CPU stack syscall/Massif attribution supplement
these static bounds; the counter or whole-process heap alone is not the proof.

Final actual entropy/HTTP command:
`VSR_VIEWER_TEST_DESTRUCTIVE=1 VSR_VIEWER_TEST_ENTROPY=1 python .scratch/viewport-integration/04-live-viewer.py barney pressure-probe`
→exit0,1/937,24 PNGs/24 unavailable from48 requests,5327939B actual maximum PNG,
112/109 submissions,124 heartbeats,247860 compared pixels,mean RGB0.404519 vs
flipped2.11139. Two positive HTTP200/result responses stall behind receive-window
backpressure; held leases grow from35504802 to174310742B, source owns at most
183149386B, then both drains release. Cancel/READ withdrawal protects historical
and in-flight refs; after withdrawal9158744B vs160022590B before, natural viewing
continues. Secondary acquire4.54198ms,setup5.59356ms,release2.76153ms. No cap/encoder
change. Previousc8 near-ceiling entropy/HTTP evidence remains preserved, not replaced
by this lower charged sample or a debugger/Massif timeout.

## Actual final production vsrViewer — native pixels and lifecycle

Production now explicitly attaches/exposes **SDL_CPU_READY_V1 / VIEWER_CPU_READY**
per instance after setup/connection succeeds. Qualification is not a barney-name
whitelist. Every actual transfer receipt, device-instance generation/render-start
receipt, native/output/effect profile and presentation-time framebuffer/renderer
scale is rechecked. Normal HOST or MANAGED_HOST SDL upload only; unknown/absent
path, interop/GPU-only bytes, unknown output/effects and non-unit scale stay
explicitly unavailable, not read back or replaced. Profile details/enforced
negative controls are preserved inc9 and `[viewer-presentation]` regressions.
Non-unit scale rejection is **not** high-DPI parity qualification. Backend/library
selection and its natural fallback behavior are unchanged; an image always names
its actual rendered device and is never fetched from the independent AgentView.

Private diagnostic helper compilation:
`c++ -std=c++17 -O2 -fPIC -shared .scratch/viewport-integration/04-production-oracle.cpp $(pkg-config --cflags --libs sdl3) -ldl -o .scratch/viewport-integration/logs/04-production-oracle.so`.
Existing `04-production-ui` writes only newly isolated XDG UI State (HUD/axes/time
slider disabled for the diagnostic). It is not a user's settings change. The
oracle reads only the private SDL framebuffer before real RenderPresent and never
feeds any pixels into VSR/MCP. Real XTest mouse events operate only this authenticated
loopback fixture/window; command file scale changes only this private SDL renderer.
No source-only fake close hook, new controller or simulated registration is used.

Final commands:
`VSR_PRODUCTION_LIFECYCLE=1 python .scratch/viewport-integration/04-production-viewer.py {barney,helide} production-c10-final`
→each exit0. `04-{barney,helide}-production-c10-final-proof.json` records:

- actual installed production vsrViewer -> native PiToolDefinition -> client ->
  stdio proxy -> HTTP -> admitted FrameTools: each31 PNGs/1 cold,758x229 and278x139,
  92000 independently sampled comparison pixels;
- max mean RGB error barney1.3039966 / Helide2.6445578 (unchanged8-unit threshold),
  opaque alpha, top-left/sRGB, flipped/black negative controls, actual chosen
  barney/Helide device/render-start/pipeline/output/submission bases;
- identical historical metadata+PNG after resize; presentation==request-observed
  presentation, no `vsr_render` request / Scene write / replacement image;
- actual UI closure unregisters old identity; old status is minimal/result invisible
  and old image denied; UI View-menu reopen registers fresh ViewId2/new ViewIdentity,
  produces another native PNG; old identity/reference never revives;
- actual SDL renderer scale2 returns **unsupported_display_scale_profile** with
  real dimensions/unchanged advertised limits and no image; scale1 restores a
  naturally presented available frame;
- real Agent Activity **whole-session disconnect** withdraws current READ authority
  and denies the retained fresh image at transport/session boundary. This is not
  mislabeled as narrow Scene READ withdrawal; the latter is separately exercised
  on the same source by actual SDL/native fixture/public tests;
- final bounded busy retries0 each. Diagnostic oracle receipt delta max12.0780 /
  14.9950ms, total client PNG/oracle roundtrip max163.0205 /161.3327ms; these are
  delivery/diagnostic numbers, not UI callback or GPU polling latency guarantees.

Failed experiments remain disclosed: firstc10 production oracle42.3476 error after
resize, then private input diagnostic attempts. The original1280x960 Xvfb was
smaller than initial1920x1080 viewer; resizing left its origin at(-320,-60), clipping
its display and making top-menu test coordinates unreachable. The corrected private
server fits1920x1080 and moves its resized window to(0,0); no image threshold was
loosened. Ring oracle selects the latest manifest record for a reused BMP path,
not old rows that can name newer bytes. It waits400ms for private resize geometry
before later comparisons, while public latest/resize/stale tests cover transition
frames. Early manual lifecycle pauses exceeded the old90s client timeout; final
fully automated lifecycle has no pause. Earlier Helide MCP busy run failed; final
helper permits finite80x25ms pre-admission busy retry of identical arguments/ref,
reports retries, and final runs needed0. These failures are not positive images.

## Memory mistakes, corrections and honest tool limits

New test evidence initially dereferenced Viewport counters **after Application::run
had destroyed its windows**. Genuine Memcheck red: disabled261 errors/9 contexts,
enabled264/12. Owned invalid reads in `test_IntegrationViewerLive.cpp` were not
waived as driver errors. Fixed by snapshotting scalar counters before teardown,
then all after-run assertions/logs use owned scalars. Failed logs retained as
`04-c10-owned-red-{disabled,enabled}.log`. An attempted freeze check called a
protected API and failed to compile; removed rather than invent a public test seam
or count a build failure as behavioral RED. Activation now consumes a one-shot
setup publication, not an every-frame reactivation policy.

Final enabled/disabled GUI Memcheck commands:
`python .scratch/viewport-integration/04-live-viewer.py barney {baseline-memcheck,memcheck}`
Both fixture assertions pass (1/246 disabled,1/738 enabled), but **both tools fail**
child99/wrapper1:261/257 errors,7 origin contexts each,2064B DBus definite leak,
30968B possible/214 blocks,0 indirect loss. Positive audit
`python .scratch/viewport-integration/04-driver-audit.py logs/04-baseline-memcheck-barney-viewer.log logs/04-memcheck-barney-viewer.log 04-c10-driver-audit.json`
(with workspace-qualified paths) →exit0,all7 contexts positively match. External
XCB/Mesa/OptiX/DBus origins remain recorded; no suppression, clean GUI/GPU assertion
or unexplained owned invalid access. Prior Compute Sanitizer CUDA999/OptiX7001/
SIGFPE and300s maximum Massif timeout remain **unsupported/failed instrumentation**,
not silently upgraded to clean qualification. This qualification instead combines
owned static bounds, clean CPU tests and positive differential allocation/migration
traces as expressly allowed by this dispatch. Whole-process RSS/VRAM is not capped.

## Final enabled regressions / formatting

`cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`
→exit0 before runtime, logs `04-c10-final-{build,install}.log`.
With own installed libraries first/local TMPDIR, serialized:

- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`
  →68/68,72.77s (`04-c10-qualified-ctest.log`); earlier pass68/68,72.61s;
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests` →355 cases/125491 assertions
  (`04-c10-qualified-integration.log`);
- same executable `'[request-guidance],[view-info],[view-identity],[presented-frame],[viewer-presentation],[translation-preview]'`
  →49/8264, covering01/02/03/04/05 (`04-c10-qualified-01-02-03-04-05.log`);
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation]'`
  →exit0,26/4004,0 errors/0 definite/indirect,26976B possible/180 blocks and
  ioctl/address-range warnings disclosed (`04-c10-qualified-headless-memcheck.log`);
- `clang-format --dry-run --Werror` on5 touched C++ files →exit0. Full owned-series
  formatting check repeated before commit; its count recorded on the ticket.

Prior StudioRemote count4!=5 transient remains disclosed inc7/c8/c9; neither this
dispatch's full serialized pass failed it, and no test was loosened. Assertions
with bounded polling vary; only actual run totals above are claimed.

## Cumulative view/frame coverage map for final review

| Spec stories / decisions | Positive proof / public regression |
| --- | --- |
|1–22, role/identity/binding/effective versus selected|01/02 proof, `[view-info],[view-identity]`, final independent AgentView + real production current/rendered device/camera/renderer metadata; no metadata readback/render side effect|
|23–28, actual presented final image/color/orientation/effects/UI exclusion|03 real pipeline/SDL tests, c7/c8/c9 actual Outlines/renderer/camera/gamma image comparisons, final **production** Pi/HTTP PNG parity and native dimensions; profile rejects unsupported effects|
|29–39, identity/latest/stale/replay/resize/retirement/multi-view|03 request-bound/delayed-copy negative controls, c9 actual start/upload/checked submission receipts, final generation2→3/secondary2 destructive tests, explicit source selection and production fresh identity/old-pointer denial|
|40–45, authority/cold/active viewing/CPU-GPU ownership/bounded entry|03 active-observer/authority/cancel tests, c9 unknown/interop path refusal, final exact disabled/on entry RED→GREEN, native whole-session+narrow public READ withdrawal, normal CPU-ready borrowed-byte path and measured callbacks|
|46–50, aggregate limits/overhead/transport/bounds|03 static complete ownership/stack/syscall/memory proof; c8 near-ceiling2-view/entropy/HTTP; final positive Nsight allocation/migration attribution and entropy drains; all2048axis/2073600pixels/8MiBPNG/12MiBresponse/16MiBproxy/256MiBowned limits unchanged|
|51–58, historical authority/cancel/expiry/no fallback/full live evidence|03 retained/cancel/expiry/slow consumer tests, c9 real Viewport destruction/replacement and weak-lifetime callbacks, final production close/reopen/retained current-authority denial and native actual barney+Helide pixels; no fresh capture or quality/sample claim|
|109–113,119, applicable real tests/memory/qualification|final enabled355-case integration/68 CTest; CPU Memcheck0 errors; external7-context GUI errors positively attributed, not clean; positive maximum CUDA witness plus honest sanitizer/Massif/debugger limits|

Inspection/translation milestones remain separately owned. This map does not claim
06 completion or final whole-spec review. Unsupported interop/GPU-only storage,
non-unit scale parity, arbitrary effects/drivers and monitor/compositor behavior
remain explicit limits; **current actual barney native presented pixels are no
longer an unavailable/CPU-only partial goal** for the admitted tested profile.
