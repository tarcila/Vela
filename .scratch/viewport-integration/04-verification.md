# 04 — Checked viewer presentation WIP and qualification boundary

**Outcome: ready-for-human, coupled implementation continuation; NOT resolved.**
No spec decision/hole, inaccessible credential, independent-review-route hole or
new ticket/split is asserted. Real barney is available. Its requested presented
MCP image acquisition/parity is still unfinished, not excused as unavailable.
Final whole-spec independent review remains the outer orchestrator's post-merge
gate. Author inspection is not independent review.

## Lineage, isolation and scope

Workspace `/home/tarcila/Code/ANARI/vela/viewport-gpu-frame`.
Before source changes and again before recording, exact worker parent and the
unchanged `viewport-integration` bookmark were
`0ba3ee6c9a50ee37303cd0a4bb21594f2a62d390` (`mluszwwu`).
Spec SHA256 remains
`e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
Read the full base workflow/spec/04, resolved03 ownership/verification records,
02 binding evidence, actual producers and FrameTools. No retry WIP.
No spec/addenda/sibling/historical ticket/ledger/executor/config edit, bookmark
move, other workspace/cache change, remote action or delegation. No user viewer,
MCP lease/controller, Scene, camera, device or preferences were accessed/changed.
Fixtures own authenticated Xvfb, isolated XDG runtime/config and local TMPDIR.
Raw logs/descriptors/configuration remain ignored/local; proofs contain no
credentials or raw ANARI handles.

## Preserved source work

- Copy-To-SDL now separates successful upload, checked quad enqueue and successful
  `SDL_RenderPresent`. Fixed-size value receipts contain no color-buffer pointer.
  Upload alone, failed submission, no queued draw and wholly clipped output do
  not invent a presentation. Resize retires receipts; counters are local to the
  pass instance, NOT globally unique integration Frame Identities.
- The real Viewport draws the same white, bottom-left-UV quad via an ImGui draw
  callback using checked SDL geometry. The generic ImGui backend ignores that
  call's return value. Application forwards its actual final submission result;
  hidden windows also get cleanup. Renderer scale and ImGui framebuffer scale
  follow the backend's mutually exclusive scaling rule, with state reset after
  the callback. UI chrome, HUD and gizmos remain separate draw commands.
- Normal CUDA display transfer is ordered on the actual pipeline stream. Map,
  array copy, unmap and completion errors are checked. The existing host fallback
  completes preceding kernels before SDL accesses managed memory. These waits
  belong to natural display and are NOT bounded readback qualification. No new
  GPU staging/download, snapshot copying or main accumulation reset was added.
- ANARI Scene Pass can capture opaque immutable host state at each actual ANARI
  render start and retains the state of successfully copied pixels, including
  async older images. Resize removes copied state; failed/missing capture stays
  unknown without preventing natural render. Rendering has no integration wire
  dependency. The provider is optional/default absent; the production viewer
  does NOT yet attach qualified Scene/camera/configuration receipts. Providers
  must be bounded and handle initial background setup safely; they must not
  inspect live UI/Scene there. Opaque fixture values are not provenance proof.
- The real viewer registers frame tools and two explicitly UNQUALIFIED viewport
  sources on one existing bounded FrameService. Such a source cannot inherit
  synchronous CPU exposure. Authorized requests return the constant truthful
  `viewer_acquisition_unqualified` reason, never an independent render/image.
  Independent AgentView and legacy render remain distinct. Default exposure off.

This is the real viewer submission foundation, not a completed frame adapter:
no returned viewer PNG, GPU acquisition, target pin or qualified rendered
provenance is claimed. All acceptance boxes except fixture isolation remain
unticked. Do not enable sources from these partial results.

## TDD and external seams

New public rendering tests use actual ImagePipeline/SDL software submission and
literal asymmetric decoded surface pixels. They distinguish upload/queue/final
submission, later unshown production, clipping and resize. Real Helide ANARI
frames exercise old/current opaque start-state association and failed capture.
An admitted public integration test proves explicit unqualified viewer rejection,
no image and unchanged CPU capability. Actual SDL Application/Viewport tests use
real Scenes and devices, not a presentation simulator.
Red logs: `04-red-build.log` absent receipt APIs (after correcting Catch include),
`04-state-red-build.log` absent ANARI state API, `04-qualification-red-build.log`
absent qualification gate, `04-live-red-build.log` absent Viewport provider API,
`04-clip-red-test.log` a genuinely accepted-but-wholly-clipped draw (1 assertion).
Each was followed by a green build/test; no absent-match claim.
The first live fixture crashed because it omitted base `setupWindows()` modal
initialization; a private GDB run identified it and the fixture was corrected.
GDB exit0 is NOT a passing test or GPU proof. Initial native fixture geometry
selection/resize assumptions also failed and were corrected, not counted.

## Exact final verification

Every shell cd'ed to this workspace. Runtime environment:
`LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH"`,
`TMPDIR="$PWD/.scratch/viewport-integration/tmp"`. Full tests serialized.

1. `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`
   → exit0, own warm all-target build/install before final runtime. No cold
   configure/shared tree/disabled features. CUDA12.9, UI, SDL3, networking, Lua,
   integration/MCP and interactive apps remain enabled. Logs `04-final-build.log`,
   `04-final-install.log`. No output/caches deleted; about5.1GiB free.
2. `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`
   → exit0, **68/68**,72.69s (`04-final-ctest.log`).
3. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation],[view-info],[view-identity]'`
   → exit0, **31 cases/6464 assertions** (`04-final-focused.log`). Includes all
   20 retained03 cases, actual slow HTTP drains/cancellation/multi-source memory,
   new qualification rejection and rendering cases, and02 regressions.
4. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests`
   → exit0, **352 cases/125222 assertions** (`04-final-integration.log`).
   Includes01/02/03/05 and legacy/public regressions; no final06 or whole-spec
   qualification inference. Poll/heartbeat assertion counts vary.
5. `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[presented-frame],[viewer-presentation]'`
   → exit0, **23 cases/3879 assertions;0 errors;0 definite/indirect lost bytes**.
   **23952 bytes possibly lost/173 blocks;23475909 reachable bytes**,0 suppressed
   errors. ioctl/address-range warnings disclosed (`04-final-valgrind.log`).
   Memcheck observes host allocations, not GPU kernels/driver memory; no sanitizer,
   GPU memory, physical-OOM or new Massif attribution qualification claimed.
6. `clang-format --dry-run --Werror $(jj diff --name-only | grep -E '\\.(cpp|hpp|h)$')`
   → exit0 on **14 changed C++ files** (`04-format.log`). CMake source links UI
   only when enabled; no feature option changes. `jj diff --check` was unsupported
   by this installed jj; it is NOT claimed as a passing check.
7. `python .scratch/viewport-integration/04-live-viewer.py <helide|barney> probe`
   → both exit0; **1 actual SDL viewer case/11 assertions each**. The test is
   hidden `[.viewer-live]`, deliberately excluded from ordinary CTest/full default
   to prevent opening a user's display; these explicit isolated runs cover it.
   Both exercised resize and same-Identifier source retirement/recreation.
8. `python .scratch/viewport-integration/04-live-viewer.py <helide|barney> native`
   → both exit0 against the actual freshly installed-library viewer and native
   Pi MCP client/proxy. Each completed **13 explicit-view unavailable frame
   observations**,12 image retrievals with zero image items, two distinct viewport
   identities, unchanged Scene/View revisions/camera/renderer/device/dimensions
   during observation, real resize and default exposure false. Helide additionally
   retrieved one explicitly independent legacy PNG. Proofs `04-*-native-proof.json`.

## Measured path and limits (no qualification inflation)

Real SDL Viewport probe on NVIDIA RTX5880 Ada with these private Xvfb instances
reported **managed_host_sdl_upload** for both devices, not CUDA–GL interop:

| Device | Owning UI heartbeats | Checked submissions | Max/mean normal upload ms |
|---|---:|---:|---:|
| Helide |34|31|10.0772 /0.460115|
| barney |46|31|5.90235 /0.307930|

These are actual sampled natural-display upload durations, not acquisition setup,
readback/drain latency, percentiles or quality/sample counts. The source pipeline
uses CUDA-managed storage; the fallback SDL upload already consumes host-readable
storage after stream completion. Its stability/migration and safe copy ownership
for observation still require qualification. No extra download was introduced.
CUDA–GL interop was not exercised/qualified here. Native viewer MCP round-trip
(max/mean) was39.946629/31.943888ms Helide and39.768354/26.418531ms barney for the
12-loop unavailable requests; it is NOT GPU acquisition latency or heartbeat proof.
Native size758x229/display1011x305.434 changed to278x139/display371x185.5; framebuffer
scale[1,1]. This proves actual metadata resize, not returned-image scaling parity.

Normal maximum CPU regression (full integration):1920x1080 produce+submit68.9603ms,
request pin0.046783ms,61 heartbeats,82441-byte PNG, sampled reservations162041495
bytes. Instrumented Memcheck produce+submit7586.15ms/pin0.986525ms is NOT normal
latency evidence. Shared256MiB infrastructure/stack/snapshot/result/HTTP ownership
and existing2048axis/2073600pixels/8MiBPNG/12MiBresponse/16MiBproxy bounds remain03's
contract and passed its regressions. The new qualification enum is charged by
`sizeof(View)`; no viewer/GPU staging or retained frame is allocated/exposed yet.
No new complete observation-owned GPU budget/allocation proof is claimed.

## Cumulative view/frame mapping and remaining coupled work

| Spec stories / decisions | Public evidence now | Production viewer limit |
|---|---|---|
|1–22; decisions2–4|02 identity/effective binding and legacy tests; full352; real native Helide/barney metadata|Metadata only, not image-producing binding|
|23–28; decisions5,10|03 independent decoded CPU PNG/Outline/color/alpha tests; new actual SDL asymmetric quad oracle|Viewer PNG/effects/color/native scaling comparison absent|
|29–35; decisions5–6|03 request-point replay/delayed copy/reuse controls;04 checked upload/draw/submission and opaque ANARI start-state tests|No joined rendered Scene/camera/device/config/Scene stamp and snapshot at this seam|
|36–39; decision7|03 retained resize/source lifetime/device replacement tests;04 actual same-Identifier replacement/resize|Per-pass IDs must join a view/source generation; no GPU latest retirement/drain yet|
|40–46; decisions9,11–12|03 permission/activation/budget/withdrawal/drain tests;04 unqualified guard and real negative MCP|Owned CPU/managed/interop acquisition, event polling/reuse/cancel/release not implemented/qualified|
|47–49; decision12|03 arithmetic, max native PNG, high entropy, actual slow HTTP pressure; full focused31|No cap increase; new GPU staging must share the same aggregate budget|
|50–55; decisions6–9|03 admitted observe/image/status/cancel/replay/expiry/withdrawal;04 real unavailable tools/no substitution|No available viewer operation result; real GPU lifecycle/authority matrix still required|
|56–58; decision24|Legacy independent image preserved; explicit unqualified rejection; actual probe heartbeats|No fresh render/screenshot implementation; real barney image goal remains unfinished|
|Testing1–7,14–15|All above, actual enabled build/install/full CTest and Memcheck|No actual MCP viewer pixel comparison, GPU acquisition instrumentation or full live camera/renderer/pipeline matrix|

At the40-minute stop assessment the remaining end-to-end acquisition + matched
rendered/public provenance + request-bound latest + source-generation retirement +
shared memory/drain + actual image/pipeline parity matrix cannot honestly fit the
remaining20-minute working budget. Preserve this verified coupled WIP on04,
not a helper-only split and not a spec/human-decision hole. A continuation must
complete that same adapter before enabling any path or resolving04. Real barney
availability and these successful partial tests do not waive that requirement.
