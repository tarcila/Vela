# 04 continuation9 — production candidate proof and blocking lifecycle fault

Outcome **ready-for-human**. Preserve the complete owned CPU03/04 stack. This is
not resolved, not a completed GPU/profile qualification, and not a new spec or
permission hole. Final production sources are **VIEWER_UNQUALIFIED/unexposed**.
Final whole-spec independent review remains with the outer orchestrator.

## Lineage / environment

Dispatched integration bookmark/base `4eb0f40656da4170e555c4972b67df7e412d86f9`
positively matched in ancestors(@) before changes and before recording. Parent
`rvuluoyo`/`34bd4c73` retains salvaged `mnutxuvx`/`43fe70d3`, earlier04, and CPU03.
Spec SHA remains `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
Every shell cd'ed here. Own warm RelWithDebInfo j3 build/install; own installed
libraries first and workspace-local TMPDIR/runtime/configuration/logs. No cold
configure, disabled feature, other workspace/cache/configuration, bookmark move,
user viewer/controller/Scene/camera/device manipulation, sibling/historical ticket,
spec/addendum/ledger/executor, encoder/cap/global-store or remote edit. CUDA/UI/
network/Lua/SDL3/MCP remain enabled. Raw logs ignored; proof records explicitly tracked.

## Enforced real transfer/profile boundary

`ViewerFrameSource` previously discarded its actual callback path argument.
`FrameService::acquireViewerPixels()` now accepts that receipt and rejects an
absent/unknown path or interop before binding/pixel access or snapshot allocation.
Only normal successful `host_sdl_upload` / `managed_host_sdl_upload` callbacks
are candidates for SDL_CPU_READY_V1. The real pass already consumes the exact
CPU-ready bytes in normal SDL_UpdateTexture before the callback, serially on the
pipeline owner; it does not reuse them until callback return. Observation adds no
new CUDA driver operation at that seam. A pointer/declaration alone cannot borrow
qualification. Unsupported transfers withdraw exposure and require reattachment.

The new public admitted-source regression, using a deliberately unreadable pointer
and no real transfer receipt, genuinely failed **2/13 assertions** before the fix
(`04-c9-path-red.log`). After the fix, viewer-presentation passed **5 cases/123
assertions**. Other additions test same-Identifier instance mismatch, output/AOV
rejection before source access and presentation-time scale retirement. They were
not individually run red against the original source and are not claimed as such.

Real `Viewport` now supplies a host-local device lifetime generation advanced at
actual teardown, not inferred from library name. Upload acquisition requires it
to match the actual-render-start receipt. It is not serialized as an address.
The candidate output profile requires real viewer Image Pipeline, nonzero instance,
rgba8_srgb, beauty/ACES/exposure0/no auto-exposure, tested gamma2.2 or1.7, output
transform enabled and no primitive/bounds outlines. Ordinary selected-object
Outlines are covered by actual CUDA image tests. Other effect variants are
unavailable at the callback seam rather than merely described as unsupported.
Normal viewing is not reconfigured by these checks.

Successful draws carry presentation-time framebuffer/renderer scales. The
candidate currently rejects non-unit scales with
`unsupported_display_scale_profile`, clears latest and never returns an old copy
as that newer presentation. Software SDL verifies distinct draw/upload receipts
and scale2/1.5 preservation; the public source test proves an acquired, already
presented unit-scale upload cannot be reused as a qualified scale2 presentation.
This is explicit rejection, NOT real high-DPI GPU parity qualification.

## Ownership / actual destruction

`Viewport` has a weak lifetime sentinel and a pre-device destruction notification.
The adapter disconnects state/acquisition/presentation callbacks before passes and
device teardown, nulls its viewport pointer, retires latest and can safely outlive
the window. Adapter replacement disconnects the old owner before installing the
new one; its later destructor cannot clear the replacement's callbacks. Failed
connection reservations do not clear another owner's callbacks.

The actual SDL fixture constructs a genuine extra Viewport, registers it through
the public interface, replaces its adapter, destroys the Viewport while its
replacement/registration survive, observes the explicit closed reason, unregisters
and drains it, then destroys the adapter. It checks each connection/pointer/lifetime
transition. This is not a mock token/private epoch test. Headless and actual live
memory evidence below exercise the resulting ownership paths.

Production `registerViewportBindings()` guards callbacks with weak lifetime,
unregisters hidden/destroyed windows and detaches their source before dropping
trackers. Reopening a live window registers a fresh ViewId rather than reusing
retired identity. No read/render grant or independent-agent role changed.
**Actual native production close/reopen identity/replay/withdrawal proof remains
pending**; do not equate the real destruction fixture with that full UI test.
Final discovery has one active interactive registration, not a fabricated second
hidden viewport. The negative client explicitly records this count while preserving
all unavailable/no-image/unchanged-metadata/legacy assertions.

## Actual production native images — candidate run, not final exposure

An isolated authenticated Xvfb instance ran the installed **actual vsrViewer**
with the same ViewerFrameSource and FrameService as the SDL qualification fixture.
During this private candidate experiment only, production attachment was
VIEWER_CPU_READY and explicitly exposed. After the blocking lifecycle failure,
both declarations were restored to their final guarded state. No user's instance
or configuration was involved. `productionExposed:true` in the historical proof
is truthful about this experiment, NOT the committed feature's exposure.

Commands:

- `c++ -std=c++17 -O2 -fPIC -shared .scratch/viewport-integration/04-production-oracle.cpp $(pkg-config --cflags --libs sdl3) -ldl -o .scratch/viewport-integration/logs/04-production-oracle.so`
- `c++ -std=c++17 -O2 -I src -I external .scratch/viewport-integration/04-production-ui.cpp _out/_cmake/RelWithDebInfo/libvsr_core.a _out/_cmake/RelWithDebInfo/libfmt.a -lpthread -o .scratch/viewport-integration/logs/04-production-ui`
- `python .scratch/viewport-integration/04-production-viewer.py barney production-c9c` → exit0.

The installed native PiToolDefinition/client/stdio proxy/HTTP/FrameTools route
returned **31 actual production PNGs/1 cold**. Native sizes758x229 and278x139;
**92000 independently decoded comparison pixels**, maximum mean RGB error1.29911;
flipped/color negative controls disagree. Same admitted request replay returns
identical old metadata and PNG after resize. View identity/presentation/request
association, ANARI-start/final-upload/checked-display bases, monotonic times,
actual barney Device Identifier and top-left/sRGB PNG semantics are checked.
Maximum round-trip164.454ms includes PNG/proxy/oracle and is NOT UI service latency.
Maximum diagnostic timestamp difference12.0299ms is disclosed.

The LD_PRELOAD oracle reads only the private SDL framebuffer before actual
RenderPresent, matching the actual flipped quad and nearest pre-present monotonic
receipt; it does not feed pixels to VSR/MCP, render an ANARI frame, allocate a
fallback device or implement image delivery. BMP ring bounded128 presentations.
Private UI State uses the public DataTree format to disable only diagnostic HUD,
axes and time slider in this fresh fixture config, allowing the original full
interior parity oracle rather than relaxing its8-unit threshold.

Proof `04-barney-production-c9c-proof.json`; logs `04-production-c9c-barney-*.log`.
Earlier candidate runs are failed, not counted: normal HUD contaminated the
comparison (8.0334); a central-only attempt still met the HUD after resize
(46.6604). The subsequent isolated overlay-free UI State fixed the oracle setup.
No threshold was loosened. The central-only helper is retained as a failed
experiment, not used for final production parity.

## Concrete blocker: actual multi-view reconstruction fails even without observation

- `VSR_VIEWER_TEST_DESTRUCTIVE=1 python .scratch/viewport-integration/04-live-viewer.py barney pressure-probe` → wrapper1, **pure virtual / abort**, not a test pass.
- `VSR_VIEWER_TEST_DESTRUCTIVE=1 python .scratch/viewport-integration/04-live-viewer.py barney pressure-debug` → gdb wrapper0 but inferior **Thread2 SIGSEGV**, NOT a green runtime.
- `VSR_VIEWER_TEST_DESTRUCTIVE=1 python .scratch/viewport-integration/04-live-viewer.py barney pressure-baseline` → observer-disabled **SIGABRT**, wrapper1.

Two actual active barney views are present while the primary reconstructs its
same-Identifier device/pipeline using the existing natural setup path. GDB has
positive frames in `libnvidia-rtcore`/`libnvoptix`,
`owl::DeviceContext::destroyPipeline`, `owl::Context::buildPipeline`,
barney `syncPipelineAndSBT`/`renderTiles`/`Frame::renderFrame`,
`AnariSceneRenderPass::startFirstFrame(wait=true)` and
`Viewport::setLibrary()`'s TaskQueue worker (`Viewport.cpp:410` at that build).
This identifies the failing entry path; it does NOT prove a sole driver root cause
or license ignoring a possible Scene/render-index/thread-lifetime defect.
The background provider rejects this thread before live UI/Scene observation.
Normal observer-disabled reproduction is a blocker, not an observation safety
waiver. Source guard restored. No attempt to hide/serialize away the failure in
the fixture, disable CUDA, substitute Helide, suppress diagnostics or call a
partial one-view profile the full required feature.

Logs `04-pressure-{probe,debug,baseline}-barney-viewer.log` and
`04-c9-destructive-{wrapper,debug-wrapper,baseline-wrapper}.log`.

## Final verified state

Own enabled final build/install exit0, before every runtime pass:
`cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`.
With `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH"` and
`TMPDIR="$PWD/.scratch/viewport-integration/tmp"`, serialized:

- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` → **68/68,72.85s**.
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests` → **355 cases/125229 assertions**.
- Same executable `'[presented-frame],[viewer-presentation],[view-info],[view-identity]'` → **34/6619**.
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[viewer-presentation]'` → **6/196**, exit0, **0 errors/0 definite/indirect**,22224B possible/169 blocks disclosed.
- `clang-format --dry-run --Werror` on the17 changed `src/*.{cpp,hpp,h}` files across `viewport-integration..@` → exit0.

Logs `04-c9-stop-{build,install,ctest,integration,focused,headless-memcheck,format}.log`.
Earlier preliminary section test used an early return and therefore counted only
one newly added section (151 total); fixed to allow all four independent sections.
Final196 assertions positively exercise the complete set. No old count is silently
upgraded. Prior historical StudioRemote nondeterminism remains disclosed; both
this dispatch's complete serialized CTest passes passed without changing that test.

Final actual private live commands `python .scratch/viewport-integration/04-live-viewer.py {barney,helide} probe`:

- barney **1/734**,44 PNGs/4 cold,48 requests,112 submissions/127 heartbeats;
 110436 compared pixels, mean/flip0.957924/2.43866. Acquisition0.0605ms,
 pin0.126156ms, start0.111112ms, submit0.139958ms, setup0.133116ms,
 release0.428565ms; peak charged24178429B. Normal upload5.48345ms is NOT
 observation readback latency. Cancellation/withdrawal drain while viewing continues.
- Helide **1/731**,44/4,48 requests,112/115 heartbeats;107040 pixels,
 mean/flip1.03902/3.35519; acquisition0.0793ms,pin0.21245ms,setup0.101285ms,
 release0.208682ms; charged23841144B. This is real CUDA-managed SDL display
 even with a CPU renderer, not an assertion of a GPU-free process.

Final guarded production native commands `python .../04-live-viewer.py {helide,barney} native-c9` → exit0 each, **13 unavailable frame observations/12 zero-image retrievals**, one actual live interactive identity, resize and unchanged observational metadata.
Helide also retains **one separately labeled legacy independent PNG**. Proofs
`04-{helide,barney}-native-c9-proof.json`. Final production exposure is false.

## Memory / GPU limitations are still explicit

Final live barney Memcheck `python .../04-live-viewer.py barney memcheck`:
fixture **1/733 passed**, but child99/wrapper1, **255 errors/7 contexts**,2064B
DBus definite loss,30968B possible/214 blocks. No suppression or clean-tool claim.
`04-c9-stop-live-memcheck.log` preserves this final run. Positive audit:
`python .../04-driver-audit.py logs/04-baseline-memcheck-barney-viewer.log logs/04-c9-stop-live-memcheck.log 04-c9-stop-driver-audit.json`
→ all7 origin contexts match the earlier263-error disabled baseline; same2064B
leak. This is attribution only. An earlier new-lifetime run also had255/7;
`04-c9-driver-audit.json` and preserved raw log distinguish it from the final
adapter-replacement run. No unexplained new observation-owned context is waived.

Static reachable observation path is owner callbacks → budgeted Production/
CpuFrameSnapshot CPU vectors/metadata → checked presentation → request-point pin
→ fixed-stack CPU normalization/PNG → immutable result/HTTP leases. The normal SDL
transfer, managed allocation and stream synchronization precede observation.
This is NOT independent maximum two-view allocation/migration/wait tracing.
Do not claim GPU observation-owned bytes=0 or all-process VRAM/RSS/heap ceilings
from this graph, sampled old Massif or charged counters alone. Prior actual
OptiX/144 Outline Nsight evidence, failed Compute Sanitizer999/7001/SIGFPE and
Massif timeout remain exactly limited as recorded in04-c8. No new clean GPU,
sanitizer, OOM, all-time maximum or universal third-party driver claim.

## Smallest concrete continuation, not an expanding qualification claim

1. Fix/establish the genuine two-view same-Identifier reconstruction entry path
   identified above, including actual device/render-index/Scene lifetimes and
   background first-frame warmup. Re-run destructive observer-on AND disabled
   fixtures under debugger/instrumentation; do not mask the failure by omitting
   replacement or turning off the secondary. No global store/driver patch is
   authorized implicitly by this WIP.
2. Exercise production native close/reopen/fresh identity, retained old operation
   visibility/current-read withdrawal and multiple-view selection against the
   implemented weak-lifetime registration boundary. The actual destruction test
   and historical resize replay are already positive; reuse them, don't restart.
3. Complete independent maximum2-view on/off observation-owned CPU/GPU allocation/
   transfer attribution (including managed page migration/wait) against the
   existing256MiB coupled budget/2MiB encoder stack and retained HTTP drains.
   Prior max/entropy/slow-HTTP fixtures and bounds stay preserved; don't replace
   them with private helpers or whole-process memory claims.
4. Only after these actual functional/safety gates pass, explicitly attach/expose
   the same production adapter as VIEWER_CPU_READY and repeat production native
   image/replay/withdrawal/parity/service/drain tests. Unknown transfer/effect/scale
   variants stay unavailable; currently enforced rejection is not parity.

The known lifecycle fault makes resolving/enabling now untruthful. Stop with
verified guarded WIP rather than weakening the requirement or spending the
remaining sitting on an unrelated driver/global-manager redesign. No new tickets
or spec/human holes. Author inspection is not independent review.
