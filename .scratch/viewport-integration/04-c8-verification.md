# 04 continuation8 — native real-source and load qualification WIP

Outcome **ready-for-human**, same coupled04 continuation, not resolved and not a
new product/spec/human-permission hole. Production `vsrViewer` sources remain
VIEWER_UNQUALIFIED/unexposed. Available native images below come from actual
Viewport objects in the isolated SDL test application, **not production exposure**.
Final independent whole-spec review remains pending with the outer orchestrator.

## Preserved lineage and scope

Dispatched integration tip/base `c64c5719ce9ce01c8478de72a673b97c29894e82`
was positively matched in ancestors(@) before edits and before recording. Parent
`wqwmmrqm`/`50c90f43` retains the complete `mokkvqwr`/`3c9f37c1`, earlier04 and
CPU03 stack. No reset/rebase/bookmark move or dropped source. Immutable spec SHA
`e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae` unchanged.
Every shell cd'ed to this workspace. Own warm j3 RelWithDebInfo build/install;
CUDA/UI/network/Lua/SDL3/integration/MCP remain enabled. No new configure, feature
change, other cache/workspace, global store/device manager/encoder/cap edit.
Only04's real Viewport metadata bound, real viewer qualification test and private
04 fixtures/records changed. User viewer/controller/session/configuration/Scene,
sibling/historical tickets, spec/addenda/ledger/executor are untouched.

## Actual source fix, red before green

`Viewport::effectiveView()` previously copied parameter names before checking
length, and `Any::getString()` allocated a string before checking its length.
Up to128 individually valid settings could also exceed the host's bounded
metadata allowance before FrameService validation. These are observation-owned
allocations, not Scene storage which may be silently subtracted from the ceiling.
The source now scans names only up to4097 bytes, reads string values through the
nonallocating `getStringView()`, and charges a conservative16KiB renderer-setting
wire allowance (escaping, numbers and fixed structure) before retaining fields.
Incomplete settings carry `representation_limit`; natural viewing continues.
No upper-layer dependency or policy/cap change was introduced.

Real Viewport regression adds32 actual renderer string parameters of2048 bytes
in its private Scene. Before the fix the representation assertions failed and no
qualified PNG could be produced: **1 case/1338 assertions,13 failed**, log
`04-c8-metadata-red.log`. After build/install: **1 case/721 assertions**,44 real
Helide PNGs, independent framebuffer parity, cancellation/withdrawal and natural
viewing all pass (`04-c8-metadata-green.log`). This is not a mock binding test.

## Real native tool/client/proxy images

`04-available-client.mjs` executes installed Pi `createMcpToolDefinition` objects,
McpServerConnection, stdio vsrMcpProxy, native authenticated HTTP, FrameTools and
actual Viewport acquisition/submission. Actual transported arguments are checked;
no model/provider request or user controller is involved. `04-native-oracle.py`
uses independent Pillow/libpng decoding against **pre-Present SDL framebuffer**
BMPs named by actual successful source submissions, not pipeline/encoder pixels.
Private HUD/gizmos are disabled. Top-left/color/opaque-alpha/native/display scale,
production/presentation identity, monotonic timing, actual-submit revision basis,
historical same-request/same-PNG replay, and current read withdrawal are checked.
Flipped and black-image negative controls demonstrably disagree. These diagnostic
BMPs do not implement the feature; the images are returned through native tools.

Final small actual barney: **31 native images/1 cold**, **1 case/1129 assertions**;
Helide: **31/1**, **1 case/1138 assertions**. Distinct459x297 and294x177 images and
same-Identifier retirement epochs are independently decoded. Natural scripted
resize/device rebuild/renderer/camera/Outlines now follow acquisition progress,
so they occur after the native observer activates rather than before connection.
Replay preserves old metadata and pixels; withdrawal returns minimal protected
receipts with zero image items. Latest equals the request-point presentation,
not response arrival. No fetch invokes an extra render or Scene write.
Proofs: `04-barney-fixture-native-proof.json`,
`04-helide-fixture-native-proof.json`. Small barney round-trip max143.822ms,
Helide146.387ms include client RPC, PNG delivery and independent Python oracle;
these are NOT owning-thread service latency or quality/sample measurements.

Maximum/multiple-view native barney: **26 images/6 unavailable**, **1 case/1099
assertions**; explicitly selected two live identities with1920x1080 and2048x1012
native buffers (never the illegal combined2048x1080 shape). Each has its own
tracker/adapter, sharing one FrameService and <=268435456-byte pool. Peak charged
ownership264078771B; primary acquisition max1.21968ms, secondary4.25764ms;
submission max0.04471/1.6829ms; setup/drain retain finite bounds. Independent
native/display oracle and withdrawal/replay pass. Max client/oracle round-trip
517.462ms is not a UI stall claim. Proof:
`04-barney-pressure-fixture-native-proof.json`.

Production negative/legacy smoke remains separate: `python ... helide native-c8`
passed13 unavailable observations/12 zero-image retrievals, two registered
interactive identities, real resize, unchanged observation metadata, exposure
false and one **independent legacy PNG**. `04-helide-native-c8-proof.json`.
This does not relabel the test-host available PNGs as production completion.

## Actual GPU resources, slow HTTP and instrumentation

Private real entropy plane is ANARI triangle geometry, image2D sampler and matte
material. It is rendered by barney; no injected CPU pixels/fresh fetch render.
Two active real views, old retained PNGs, normal Outlines/camera/renderer/resize,
source budget exhaustion and cancellation share the original03 pool. Real TCP
SO_RCVBUF4096 clients stop reading two large frame responses. Both positively
return HTTP200/result prefixes; their leases persist >10 natural UI heartbeats.
Closing/withdrawal drains them while viewing continues. Normal final max-axis
run: **1 case/800 assertions**,24 images/24 truthful unavailable,48 requests,
112 submissions/124 heartbeats, max PNG5327939B (<8MiB). Earlier1920x1080 entropy
run produced6341873B. Carrier12MiB/proxy16MiB and the explicit03 encoder stack
bounds remain unchanged and passing; no truncation/default-limit change.

Final normal source start max0.041951ms, primary acquisition1.3683ms, secondary
4.19813ms; primary/secondary submission0.045921/0.019358ms; request pin0.067822ms;
setup3.43988ms; final release2.35796ms. Maximum natural SDL upload7.90592ms includes
existing GPU/display work; observation reuses CPU-ready bytes after that transfer.
No observation GPU copy/event/query/driver wait/staging allocation is added.
End-to-end delivery335.665ms includes deferred encoding and is NOT a UI service
measurement. Source peak183149322B, positive stalled-drain peak174310646B,
post-withdrawal9158744B. At least20 additional natural heartbeats occur during
drains. Counters alone are not an independent heap or all-time GPU-memory proof.

Original03 allocation/lifetime proof remains binding for stacks, controls, futures,
immutable pixels, encoding overlap, engine retention/status copies, HTTP drains
and overhead; continuation7 adds viewer callback/receipt/snapshot reservations.
This source fix makes the actual renderer metadata callback bounded before copy.
No Scene, caller PNG decode/oracle or normal render allocation is mislabeled as
observation-owned. **Maximum GPU independent allocation attribution remains
incomplete.** Massif maximum entropy attempt was stopped by its300s timeout:
1 case/1437 assertions with SIGTERM, not a passing tool. It recorded74 samples,
73 detailed, whole sampled heap+overhead425564256B; this includes Scene/driver/
normal buffers/client/oracle. Do not claim it proves256MiB ownership or a clean
all-time peak. Log/profile `04-pressure-massif-barney.*`.

Live current Memcheck fixture: **1 case/719 assertions passed**,44 PNGs, but tool
child99/wrapper1: **261 errors/7 contexts**,2064B definite DBus loss,30968B possible.
Observer-disabled same-GUI/barney/path baseline: **1 case/243 assertions passed**,
tool99/wrapper1, **263 errors/7 contexts**,same2064B leak. All seven positive origin
contexts match top-three origin frames: XCB/Mesa writev, libnvoptix initialization,
NVIDIA GL zero-sized realloc/posix_memalign and DBus autolaunch. No suppression.
`04-driver-audit.py` and `04-c8-driver-audit.json` preserve exact positive stacks;
this is attribution, not a clean memory-tool/GPU gate or unsupported absent-match
claim. Earlier baseline also reproduced the original259 errors exactly.

Compute Sanitizer maximum two-view attempt **failed**: CUDA999 from natural
cudaGLGetDevices/cudaGetLastError probing, OptiX7001/SIGFPE,2 tool errors. No clean
GPU sanitizer/interop/OOM claim. Log `04-pressure-sanitizer-barney-viewer.log`.
Nsight maximum two-view actual barney run and stats **exit0**,1 case/862 assertions;
actual barney/OptiX ray/shade kernels and144 CUDA Outline invocations appear in
`04-c8-nsys-stats.log`. This is actual execution, not per-observation migration,
quality/sample or universal driver-latency certification.

## Final verified regressions

After source fix, own enabled full build/install exit0 (`04-c8-qualified-build.log`,
`04-c8-qualified-install.log`), libraries first in LD_LIBRARY_PATH, local TMPDIR.
Serialized CTest **68/68,72.82s**; full integration **353 cases/125202 assertions**;
focused presented-frame/viewer-presentation/view-info/view-identity **32/6489**.
Headless Memcheck viewer-presentation **4/110**,exit0,0 errors/0 definite/indirect,
22224B possible/32B suppressed allocation disclosed. Formatting dry-run/Werror
exit0 on **16 C++ files** across the entire owned stack. Logs
`04-c8-qualified-{ctest,integration,focused,headless-memcheck,format}.log`.
01/02/03/05 and unchanged legacy/PNG/body/HTTP/selection regressions are retained.
Prior unchanged StudioRemote4!=5 failure remains disclosed in continuation7;
both this dispatch's full serialized CTest passes succeeded without loosening it.

Fixture failures are retained, not claimed green: early simultaneous setup/reload
multi-view attempts hit OptiX/pure-virtual faults (also observer-disabled). The
fixture now sequences its single task modal and tests destructive same-Identifier
replacement separately in the single-view profile; **replacement under multiple
active views is not qualified**. Single-view oracle initially confused default
secondaryId0 with real primary view0; the actual failing parity checks were fixed
by requiring a real secondary before choosing its oracle. Native first attempts
used an occupied local controller and stale private descriptors; native mode now
owns no competing attachment and each invocation has a fresh private runtime.

## Cumulative mapping and mandatory remaining limits

Original02 identity/binding/legacy and03 full CPU frame/limit/authority/HTTP proof
remain unchanged. Continuation7 joined real render starts, immutable ready pixels,
actual checked submissions, request-point pins and epoch retirement. This dispatch
adds actual native available-image client/proxy parity/replay/current read, maximum
and maximum-axis multiple GPU views, genuine high-entropy HTTP drains and positive
driver attribution, plus the pre-copy renderer metadata bound.

Still required before production exposure/resolution: production vsrViewer
available-image qualification/parity with an explicitly bounded supported profile;
complete actual close/recreate identity and exposure-current-read lifecycle;
destructive device-instance/camera/pipeline changes under multi-view/load and
advertised high-DPI/effects matrix; independent maximum GPU observation-allocation
attribution and remaining driver/cleanup qualification without unexplained owned
invalid accesses. Interop without CPU readiness remains explicitly off. Native
test-host success, counter bounds, timed-out Massif and failed CUDA tools cannot
be silently upgraded to full requested barney/product qualification.

At the55-minute assessment these coupled gaps cannot fit the remaining working
budget. Preserve the owned code plus ticket as WIP; continue the same04, not a
helper split, invented human/spec hole or safety waiver. No new tickets/holes.
Final independent whole-spec review remains pending after outer-orchestrator merge.
