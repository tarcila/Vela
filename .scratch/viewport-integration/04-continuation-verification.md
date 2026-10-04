# 04 continuation7 — real ready-pixel adapter, still NOT resolved

Outcome **ready-for-human: preserved coupled implementation WIP**, not a new
spec/permission/API decision or helper split. Production viewport sources remain
VIEWER_UNQUALIFIED/unexposed. No production MCP viewport PNG is claimed.
The isolated actual SDL Viewport fixture now returns real Helide/barney PNGs
through FrameService, with independently decoded framebuffer parity. This is
substantial real-GPU progress, not completion of all04 acceptance criteria.
Final independent whole-spec review remains the outer orchestrator's gate.

## Lineage and isolation

Workspace `/home/tarcila/Code/ANARI/vela/viewport-gpu-frame`; every shell cd here.
Dispatched bookmark/base `917a0540a888d7ac49365ee728a60bac1808f181` was verified
in `ancestors(@)` before source changes and again before recording. Resume parent
`kzlqoyru` / `63ee6dc3` retains `qlqvltmo` / `60b96109` and the owned CPU03 stack;
none was dropped/reset/rebased. Bookmark remains unchanged. Spec SHA256 remains
`e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
No user's viewer/controller/config/Scene/camera/device, other workspace, sibling
or historical ticket/spec/addendum/ledger/executor, encoder/cap/global store edit.
All display instances use fixture-private authenticated Xvfb, runtime/config and
workspace-local TMPDIR. No remote actions, delegation or new tickets/holes.

## Joined ownership and provenance

- New ViewerFrameSource connects the real Viewport ANARI-start provider, final
  CPU-ready SDL upload and checked quad/final submission to one FrameService.
  Initial setup workers reject their thread before Scene/UI reads. Callback
  assignments/copies are synchronized; bounded issued-receipt matching prevents
  reinterpretation of a previous provider's opaque state. Disabled lifetime
  controls prevent old callbacks from dereferencing a destroyed adapter.
- Production receipts retain Scene/view/camera/renderer/device/policy at actual
  ANARI submit, not completion/poll. Final pipeline settings are captured at
  ready upload, display geometry at checked submission, current revisions at
  request. Explicit source_frame_id and revision/configuration basis fields
  distinguish these events. Incomplete dynamic effects/default settings keep
  explicit reasons; no fabricated complete configuration or quality/sample count.
- The available current barney route is **managed_host_sdl_upload**, not interop.
  Natural display already completes the dedicated pipeline stream and consumes
  the managed bytes through SDL_UpdateTexture. The optional acquisition then
  copies those same CPU-ready bytes, before any owning-thread pipeline reuse,
  into an immutable exact-sized vector. No extra CUDA download, event, staging
  buffer, stream, device creation or fresh render is introduced by observation.
  This is a qualified-path candidate proven in the isolated fixture, not a
  blanket claim about CPU residency of arbitrary managed/device pointers.
- Uploaded owned pixels remain merely staged until successful final submission.
  EVERY newer successful submission clears prior latest before checking whether
  its acquisition exists. Failed/busy/missing acquisition never revives an older
  copy. A request pins that snapshot plus actual submission identity/timestamp;
  later presentation cannot replace the encoded input. Source epochs reject
  old acquisitions after withdrawal/retirement, including same-Identifier rebuild.
  Interop without CPU readiness retires exposure and cannot silently re-enable.

## Aggregate allocation proof extension (03 remains intact)

All additional live allocations use the same <=268435456-byte FrameBudget:
viewer connection/control/callback tail **16KiB** reserved before control creation;
ANARI receipt **8*64KiB+sizeof(Production)+4096** before bounded binding copies;
ready acquisition metadata **8*64KiB+sizeof(ViewerAcquisition)+sizeof(Production)
+8192**; snapshot **4*w*h+sizeof(CpuFrameSnapshot)+8192**; submission receipt
**8*64KiB+sizeof(Production)+8192**. Every reservation also charges03 Lease/control
headers. Fixed source object, three weak receipt controls and host vector entries
fit the existing per-view256KiB allowance. Callback controls retain their own
lease through copied callbacks after disconnect, not merely a table slot.
At most uploaded and previous-presented staging copies exist per pass; consumers
and encoding retain the same immutable vectors/leases, not uncharged pixel copies.
Dimensions/arithmetic are checked before allocation. Encoder stack, scratch,
results/status/HTTP drains and shared multi-source ceilings retain03's allocation
proof and passing regressions. No observation-owned GPU allocation is added.

Independent Massif actual barney fixture:64 samples, whole heap peak198751310B;
heap+allocator overhead200278136B; plus explicitly mapped encoder stack/guard
2162688B = **202440824B**. This deliberately includes Scene, driver, SDL and test
oracle/client heap rather than removing unnamed allocations. It is a sampled
medium-native run, NOT an all-time peak or max-GPU/multi-source ceiling proof.
Normal source-owned peak23493095B barney /23841112B Helide; counter alone is not
proof. Missing max-native real-GPU/multiple-view/slow-HTTP attribution remains.

## Exact verification (own installed libraries first; tests serialized)

`cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install
_out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` exit0. Warm enabled
CUDA/UI/network/Lua/SDL3/integration/MCP tree; no cold configure/feature changes.
Runtime `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH"`, local TMPDIR.
Final logs `logs/04-cont-qualified-*`: full CTest **68/68**,72.72s; full integration
**353 cases/125175 assertions**; focused presented-frame/viewer-presentation/
view-info/view-identity **32 cases/6520 assertions**. All exit0;01/02/03/05 intact.
Formatting dry-run/Werror exit0 on **12 changed C++ files**. Earlier CTest first
run failed unchanged StudioRemote replica object count4!=5; two later serialized
full runs passed. This is disclosed, not a claim that every invocation was green.

`python .scratch/viewport-integration/04-live-viewer.py <helide|barney> probe`:
final **1 case/719 assertions Helide;1 case/711 assertions barney**, exit0 each.
Helide44 PNGs/4 cold replies; barney43 PNGs/5 cold,48 requests each,112 checked
submissions and115/127 heartbeats. Actual asymmetric sphere lighting, native
459x297 ->294x177, resize/scale, same-Identifier pipeline retirement, renderer
background, camera rotation and genuine selection Outlines. Independently decoded
PNG versus actual pre-Present SDL framebuffer:107040/104916 sampled RGB pixels,
44/43 images; mean error1.04117/0.953398, flipped negative control3.35679/2.4545.
Peripheral HUD/gizmos are disabled only in the private diagnostic UI State.
Cancellation pins immutable input, cancels before publication, then withdraws
read; slots drain, historical/new images become protected, >10 additional natural
heartbeats continue and owned bytes fall to11500920/11369848. No fetch edits Scene.

Normal Helide/barney milliseconds: setup0.105917/0.11189; ANARI-stamp max
0.133918/0.073561; owned acquisition0.137917/0.064303; submission receipt
0.120638/0.124143; request pin0.188507/0.182039; final drain/release
0.348902/0.144282; max end-to-end delivery33.4366/33.3402. Applicable owning-thread
service assertions use100ms. Native sizes are small/medium; these are measured
samples, not universal worst-case GPU qualification. Natural upload max
9.54701/5.71348 includes existing driver/display work and optional acquisition.
Diagnostic SDL readback is test-only and its latency is NOT product acquisition.

`nsys` private barney fixture exit0,1 case/711 assertions; `nsys stats` exit0.
Actual OptiX/barney ray generation/shading and **72 CUDA Outline kernels** appear
in the report; aggregate UVM Device-to-Host31.920MB/3842 events. These include
natural renderer/display/diagnostic activity, not per-observation attribution or
quality/sample units. Natural CUDA stream creation has a103.939ms outlier, NOT
covered up as acquisition setup. Report/raw CSVs remain ignored under logs/.
`python ... barney massif` exit0,1 case/718 assertions; figures above. Tool-slowed
upload/delivery timings are not normal responsiveness evidence.
`python ... barney memcheck` is **NOT a passing memory-tool gate**: fixture itself
passed1 case/719 assertions, but Valgrind child exited99 (wrapper exit1),259 errors
from7 contexts,2064B definite DBus autolaunch leak,0 indirect,30968B possible;
XCB/Mesa uninitialized syscall buffers and libnvoptix initialization diagnostics,
plus unsupported ioctl/address-range warnings. No suppression was added and no
clean GPU Memcheck/sanitizer/OOM proof is claimed. Headless `[viewer-presentation]`
Memcheck separately exit0,4 cases/110 assertions,0 errors/0 definite/indirect,
22224B possible; this is NOT a substitute for the failed live GPU tool gate.

`python ... <helide|barney> native-cont` exit0 each on final installed-library
then-current production viewer:13 unavailable frame observations/12 no-image retrievals per
device, two distinct registered interactive identities, unchanged observation
metadata, actual resize and exposure false; Helide retains one legacy independent
PNG. New records `04-*-native-cont-proof.json` preserve original native proofs.
Native-cont ran just before the final interop refusal addition, whose positive
route is unchanged; final full CTest/live runs cover that addition. Production
native available-image transport/parity remains undone, explicitly not claimed.

## Cumulative mapping and honest remaining work

02 retains roles/bindings/legacy stories1–22;03 retains all admitted CPU frame,
PNG/limit/authority/replay/HTTP/resource regressions (stories23–58), including
maximum1920x1080 normal produce+submit71.0685ms/pin0.057469ms,62 heartbeats and
82441B PNG in final full integration.04 adds the real joined viewer ready-pixel
adapter and actual GPU rendered/current/provenance/image/parity/effects/resize/
same-Identifier/cancel/withdrawal evidence described above. New interop guard's
real headless rejection regression failed2 assertions before the exposure fix
and now passes; ready-provider APIs originally failed compilation before adding
them. The initial framebuffer oracle wrongly included HUD and was corrected
through the private real UI-state interface; failures are preserved, not counted.

Still mandatory before exposure/resolution: production native available MCP PNG
and its genuinely chosen viewport comparison; multiple active real viewer sources,
maximum-native/near-ceiling shared GPU-view/slow-HTTP ownership pressure and full
source closure/recreation/current-read lifecycle; per-phase driver/migration/
release qualification under those loads; broader effects/high-DPI scaling where
advertised. Interop is unavailable/unexposed with a reason, not implemented or
qualified. No transfer/sample/call count is a quality certification. The failed
live GPU Memcheck gate and remaining memory/instrumentation limits remain honest.

At the stop assessment these coupled gaps cannot fit the remaining working
budget. Preserve and continue the same04 source, warm tree and tests; no helper
split, safety waiver, fabricated driver/API hole or final-review-route hole.
No acceptance box is newly ticked from these partial results. Final independent
whole-spec review remains pending after orchestrator merges. Exact outcome JSON
is `04-continuation-outcome.json`; the committed worker change is the mergeable
tip of the full owned series, not a replacement for03 or earlier04 WIP.
