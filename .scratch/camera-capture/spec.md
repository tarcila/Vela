# Camera & Capture: a live image without changing the main view

**Status:** ready-for-agent
**Milestones:** 1
**Scope:** view inspection, cached scene/node bounds, immutable capture, client-side PNG saving

## Problem Statement

The live-scene editing workflow now works, but an agent still cannot reliably
produce an image from a requested viewpoint. In the chess experiment, the
queen's-eye image required rebuilding the scene offline, choosing another
renderer, constructing a camera in Lua, and writing the resulting file there.
That reconstruction did not necessarily match the running viewer's background
or rendering setup.

Reading a Scene camera object does not establish which camera a registered view
uses. The viewer's current agent view has its own fixed camera and helide
renderer, separate from the interactive viewport. Existing render requests
cannot select a temporary camera, dimensions, or quality, and images are
returned without a supported client-side save operation. There is also no
bounded observation of a selected node's bounds for placing an eye above the
queen rather than inside its geometry.

The user cannot presently perform manual testing. This increment must therefore
have automated image, ownership, responsiveness, lifecycle, and file-publication
oracles rather than relying on a model's aesthetic judgment or a successful
compilation.

## Solution

Expose the actual configuration and capture capabilities of explicitly
registered views, and bounded cached/provider-owned scene and node bounds.
Let a client submit an immutable one-off capture with a world-space camera,
requested dimensions, and an explicitly supported quality contract. Use
capture-owned camera, renderer, frame, and Image Pipeline resources without
changing the main viewport, its selected camera, or persistent renderer state.

Prefer the selected view's actual device and RenderIndex through a qualified
host adapter. Report the actual Device Identifier, renderer, effective camera,
quality, and pipeline provenance; never silently substitute helide or present
an agent-device image as a screenshot of another viewport. Unsupported bindings
and quality requests fail explicitly.

Preserve operation identity, revision guards, current permissions, bounded
admission, cancellation, supersession, and safe resource release. A trusted
client adapter may save a retained successful image as a PNG in an
operator-selected directory. The VSR server gains no filesystem authority.

The first acceptance workflow is: inspect the live chess view and queen bounds,
choose a camera outside the queen's geometry looking toward the king, request
1280×720 at the supported 256-sample quality, retrieve the image, and save
`checkmatte.png` client-side. The running scene and main-view state remain
unchanged throughout.

## User Stories

1. As an agent, I want to identify the registered view I am capturing, so that an image is tied to a real live rendering setup.
2. As an agent, I want the view's actual Device Identifier, so that I do not infer its implementation from a process or library map.
3. As an agent, I want its renderer subtype and configuration provenance, so that differences from the interactive viewport are explicit.
4. As an agent, I want its effective camera and projection, so that reading an unrelated Scene camera is not mistaken for controlling the view.
5. As an agent, I want camera fields with explicit units and coordinate space, so that field of view and orientation are unambiguous.
6. As an agent, I want advertised capture exposure, quality support, and limits, so that valid requests do not require trial-and-error round trips.
7. As a user, I want a non-exposed or unsupported view rejected, so that registration does not silently broaden capture authority.
8. As an agent, I want scene geometry bounds with revision provenance, so that I can frame the relevant live content.
9. As an agent, I want a selected node's subtree bounds, so that I can place a camera outside the queen's crown.
10. As an agent, I want local and world bounds distinguished, so that hierarchical transforms are not ignored.
11. As an agent, I want empty, pending, and unavailable bounds distinguished, so that missing data is not presented as a zero-sized box.
12. As a user, I want incomplete bounds marked unavailable rather than partially authoritative, so that an omitted contributor cannot misplace a camera.
13. As a user, I want bounds work bounded and cached, so that a query cannot scan or download an entire dataset without yielding.
14. As a host developer, I want to supply cached bounds for device-only or specialized data, so that inspection does not require an unsafe GPU download.
15. As an agent, I want bounds invalidated by relevant geometry and transform changes, so that I do not use a stale box as current state.
16. As an agent, I want to specify a one-off world-space perspective camera, so that I can capture a queen's-eye view without editing persistent state.
17. As an agent, I want an eye/target form as well as an eye/direction form, so that aiming at a known point is straightforward.
18. As a user, I want finite, nondegenerate camera validation, so that malformed direction/up/FOV values cannot reach a renderer.
19. As an agent, I want the normalized effective camera echoed, so that any representational conversion is visible.
20. As a user, I want capture dimensions to affect only the capture, so that the viewport is not resized to produce a file.
21. As an agent, I want 1280×720 supported by the qualified acceptance binding, so that the original screenshot target is achievable.
22. As an agent, I want quality expressed in supported units, so that renderer samples, completed frames, and polls are not conflated.
23. As an agent, I want the 256-sample target supported by the acceptance binding, so that quality is demonstrated rather than silently approximated.
24. As a user, I want unsupported denoising or quality rejected, so that a successful response does not hide ignored settings.
25. As an agent, I want completed frames and confirmed samples reported separately, so that progress and quality are truthful.
26. As a user, I want long captures to progress without blocking normal UI servicing, so that the viewer remains usable.
27. As a user, I want backend setup and readback qualified too, so that nonblocking polling does not conceal a blocking allocation or map.
28. As a user, I want capture-owned resources, so that its camera, renderer, and accumulation cannot perturb the main view.
29. As a host developer, I want to reuse an explicitly suitable device/index, so that capture need not duplicate all scene uploads.
30. As a host developer, I want unsafe or unresponsive bindings left unexposed, so that sharing is a qualified choice rather than a universal promise.
31. As a user, I want actual device/pipeline differences reported, so that same-scene rendering is not falsely called viewport parity.
32. As an agent, I want revision-pinned admission and completion, so that changed scene or view state cannot yield a successful mixed-state image.
33. As a user, I want identical request retries to replay their retained result, so that reconnects do not render the same work again.
34. As a user, I want different camera or quality inputs under one request ID rejected, so that idempotency cannot select the wrong image.
35. As a user, I want queue time included in the deadline, so that waiting does not grant unbounded capture time.
36. As an agent, I want progress and cancellation through existing operation references, so that capture does not invent another job protocol.
37. As a user, I want cancelled, timed-out, or superseded images never published as success, so that late device work cannot resurrect abandoned requests.
38. As a user, I want capacity held until resources are safely released, so that logical cancellation cannot enable unsafe reuse.
39. As a host developer, I want partial setup failures drained safely, so that allocation errors cannot leak handles or reservations.
40. As a user, I want host/device memory and transport bytes budgeted, so that a nominally valid resolution cannot exceed finite resources.
41. As a user, I want PNG/base64/JSON overhead counted, so that an image within a pixel limit still fits the proxy's response cap.
42. As an agent, I want image-size failure distinguished from successful capture, so that an oversized result is not silently truncated.
43. As a user, I want retained capture images to share existing retention budgets, so that a new operation does not create unlimited history.
44. As an agent, I want historical image provenance preserved, so that replayed images are not described as fresh observations.
45. As a user, I want current read/render restrictions applied throughout work, so that capture never becomes a permission bypass.
46. As a user, I want read revocation to suppress image and camera payloads while preserving allowed owner receipts, so that status is not a data leak.
47. As a viewer user, I want capture progress, outcomes, and held cleanup capacity in activity, so that waiting is visible and truthful.
48. As a host developer, I want the same capture contract headlessly and in the viewer, so that Pi, Lua, and ImGui are not framework dependencies.
49. As an agent, I want successful images as owned RGBA8 sRGB PNG content, so that capture preserves the existing image orientation and lifetime contract.
50. As a user, I want file saving in the trusted client rather than the server, so that rendering does not grant application filesystem rights.
51. As an operator, I want to opt into one save directory, so that the model cannot choose arbitrary output paths.
52. As an agent, I want to request the basename checkmatte, so that the returned image can become checkmatte.png without an offline rerender.
53. As a user, I want existing files and symlink destinations protected, so that saving cannot overwrite or redirect unrelated data.
54. As a user, I want complete atomic PNG publication, so that failure does not leave a partial file that appears successful.
55. As an agent, I want a save receipt with the actual output path and source reference, so that the artifact is traceable without copying credentials.
56. As a user, I want image expiration or lost authority reported without a new render, so that saving cannot silently capture different state.
57. As a maintainer, I want existing render/edit/restore and bootstrap contracts preserved, so that one-off capture does not regress broad editing.
58. As a maintainer, I want deterministic automated camera/image and lifecycle oracles, so that acceptance does not depend on manual testing or model taste.
59. As a maintainer, I want CPU and CUDA configurations and qualified real backends exercised, so that CUDA kernel success is not confused with ANARI capture success.
60. As a user, I want the live queen's-eye capture-and-save workflow demonstrated end to end, so that the first screenshot experiment no longer needs Lua, process inspection, or asset reconstruction.

## Implementation Decisions

### 1. Ownership, authority, and compatibility

- Introduce capture as an additive host-owned integration capability. Registered
  views explicitly expose a capture binding and validated policy; defaults are
  unexposed for unrelated/custom hosts. The viewer explicitly registers a
  binding for the selected viewport whose backend has passed qualification.
  An existing separately configured agent view may also be registered, but its
  difference from the viewport must be explicit.
- Capture uses existing current view read/render and registered-scene read
  authority, logical ownership, private session proof, and host ceilings. Host
  exposure does not grant rights; a model cannot enable it. Do not borrow scene
  edit permission to write persistent view state or add a new authentication
  mechanism. Local host submissions obey the same policy/lifecycle without an
  external controller slot.
- Scene cameras, viewport manipulator/selected camera bindings, and standalone
  agent cameras remain distinct ownership roles. An explicit capture changes
  none of them. No persistent camera editing, viewport reset, renderer-parameter
  edit, or journal/undo operation is introduced.
- Legacy `vsr_render` retains view-owned dimensions, its existing limit and
  argument shape, and existing images/results. Capture dimensions are separate;
  no silent resizing, clamping, scene replacement, or device substitution.
- The strict launch descriptor schema, credential handling, direct loopback
  routing, and unrelated source archives/Application Dumps are unchanged. The
  Studio Project/Shot protocol is not this capability and is not modified.

### 2. View observations and provenance

- Add admitted `vsr_view_info` with `request_id` and `view_id`. It returns the
  view's scene identity, matched scene/view revisions, capture-policy revision,
  effective camera/projection, actual Device Identifier, renderer subtype,
  renderer-settings and Image Pipeline configuration provenance, supported
  quality units, capture exposure, limits, and any unavailable reason.
- Observe the actual registered host binding at a safe point, not an arbitrary
  default Scene camera or a guessed library. A selected Scene renderer/camera
  handle may be included when genuinely bound; raw ANARI handles, addresses,
  credentials, and unrestricted process/file information are not exposed.
- Camera position and orientation are world-space. Perspective FOV is explicitly
  degrees in the public capture contract, converted to the device convention
  by the host. Aspect is derived from dimensions. Other supported projections
  can be described read-only; this increment's capture camera is perspective.
- Renderer/pipeline provenance distinguishes the actual source view from a
  configured agent binding. Include complete bounded effective quality settings
  and configuration revisions, not a claim of pixel-identical viewport parity.
  Unknown optional device-private information is marked unknown. A missing
  correctness-bearing binding/configuration cannot be fabricated.
- Advertise policy in authorized discovery/view info and tool metadata, never
  by extending the strict private descriptor. Returned-state changes invalidate
  the appropriate observation revisions; policy changes invalidate old policy
  tokens. No GPU work or renderer creation occurs merely to inspect view info.

### 3. Cached scene/node bounds

- Add admitted `vsr_bounds` with `request_id`, `scene_id`, optional `node_handle`,
  and optional `include_disabled`. Scene queries default to active layers and
  effectively enabled contributors. Node queries default to the entire existing
  subtree, even when hidden, and report effective enablement separately; an
  explicit include/exclude choice is bound to request identity.
- Return `available`, `empty`, `pending`, or `unavailable`, target/scene revision
  provenance, source kind, and world-space axis-aligned min/max when available.
  Node results also provide a local-space subtree box when meaningful. Empty
  geometry is not a zero box. Incomplete/unsupported contributors do not yield
  a falsely complete union; unavailable results identify the reason.
- Bounds are caches or explicit host-provider values, qualified by current node,
  structure/ancestor, geometry/reference, and relevant array-content lifetimes
  and versions. Removal/reuse, geometry changes, array writes, and transform or
  visibility changes invalidate the appropriate result. Cached stale boxes
  never appear as current. Cache eviction is allowed and visible as pending or
  unavailable, not resurrected identity.
- Initial portable coverage includes host-resident imported triangle geometry
  and supported analytic primitive extents, with shared-object instances and
  node/subtree transforms. Unsupported specialized geometry, proxy/device-only
  arrays without a current host-provided box, or ambiguous transform-array
  placement report unavailable. No arbitrary GPU download, raw-pointer export,
  array export, or renderer convergence is required by read-only bounds.
- Incremental cache construction is host scene work with explicit counters:
  default at most256 node/geometry steps or4096 vertex values per service slice,
  at most4096 nodes and1,048,576 vertex values per operation, five-second
  admission-to-completion deadline, and8MiB shared temporary bounds scratch.
  These are validated host limits. Reaching a total/deadline limit returns a
  truthful unavailable reason or timeout; it does not publish a partial box.
  Safe cache-build progress may be retained only within a bounded cache budget.
- A driver bounds acquisition is a separately qualified provider lifecycle.
  `ANARI_NO_WAIT` alone does not establish boundedness or GPU-work-free behavior;
  current blocking world-bounds helpers must not be called inside inspection.
  Provider results must identify the revision they actually cover. Failed scene
  synchronization does not forbid CPU scene inspection, but an old ready-view
  provider box cannot be mislabeled as the newly committed scene.

### 4. Immutable camera and capture envelope

- Add admitted `vsr_capture` with `request_id`, `view_id`, `scene_revision`,
  `view_revision`, `capture_policy_revision`, camera, width, height, quality, and
  optional denoise. Add operation kind `capture` with covered targets equal to
  the view and its registered scene, and use existing status/cancel references.
- Camera is either `view`, meaning a matched host camera snapshot, or an explicit
  world-space perspective value. Explicit camera contains position, up,
  `fovy_deg`, and exactly one of direction or target. Eye/target does not mean
  node centre or bounds implicitly; a client may derive a point from observed
  bounds. A view camera of an unsupported projection rejects, not converts.
- Validate finite binary64 inputs and float32 representability without clamping.
  Reject zero direction, target equal to eye, degenerate/near-parallel up basis,
  unsupported projection, malformed fields, and FOV outside the open interval
  zero to180degrees. Normalize direction and orthogonalize/normalize up using
  binary64 intermediates. Before orthogonalizing, require the normalized input
  direction/up cross-basis magnitude greater than1e-6; then recheck the final
  float32 camera is finite/nondegenerate. FOV rounding
  to zero/180 is invalid. Echo the effective camera and aspect actually used.
- Quality has explicit unit and count. Supported units are `samples` (confirmed
  backend accumulated samples per pixel under its qualified contract) and
  `frames` (completed fresh accumulation frames). A binding advertises only
  units it can deliver. The acceptance binding must implement samples256;
  replacing that with256 polls or frames without a proven mapping is forbidden.
- Requested denoise is Boolean, default false. A true request needs a qualified
  supported renderer/pipeline operation and is reported as actually applied;
  silently ignoring an unsupported parameter is not success. Unsupported
  quality/projection/denoise rejects before backend setup.
- Request identity covers all original semantic inputs and effective defaults,
  revisions, source binding, camera form/values, dimensions, quality and denoise.
  Use the existing canonicalization/high-water/replay rules, including parsed
  binary64 values rather than only rounded state. JSON whitespace/member order
  and equivalent numeric spellings do not make a new request. A retained
  identical request never starts another capture; changed input under its ID
  is `request_id_conflict`.

### 5. Policy, reservation, and transport limits

- Default axis maximum2048, total pixels2,073,600, quality count maximum1024,
  and capture deadline120seconds including queue time. Default encoded PNG
  maximum8MiB and complete image response maximum12MiB, below the existing
  proxy16MiB cap. The1280×720/256 target fits;1920×1080 can fit when the backend
  supports it. Axis limits alone do not permit2048×2048. All products/sums and
  base64 expansion use checked arithmetic.
- Default maximum one queued/active capture per view and one active capture
  globally, with256MiB capture-owned host budget and256MiB capture-owned device
  budget. Host values can be narrowed or deliberately raised only after policy
  validation and compatible transport/resource qualification. Limits and units
  are discoverable; requests cannot override them or choose arbitrary devices.
- Before admission, form an internal value-only resource/support quote: all
  explicit frame/AOV channels, backend capture allocations, host/device scratch,
  normalization, PNG/base64/JSON copies, metadata and required retention.
  Snapshot identity and configuration accompany the quote. Admission revalidates
  it, reserves capacity atomically, and records the operation. A public quote
  tool is not required; if exposed later it grants no authority/reservation.
- Bound capture-owned allocations honestly. Backend-private cost estimates and
  unknowns are explicitly distinguished from enforced bounds; a binding cannot
  promise a required hard limit it cannot establish. Existing shared scene/index
  allocations are not magically free, and deliberate independent-device/index
  duplication needs its own accounted policy and qualification.
- Static request/known-size/quality/policy failures reject before allocation and
  ID consumption under existing admission rules. Live revision/readiness/target
  checks remain at the host safe point. Busy resources reject or terminate
  honestly according to the point at which availability is established.
- Encoded size is checked before image publication/transport. Reserve a
  conservative complete encoding bound for the default RGBA8 policy; validate
  any configurable policy against encoder overhead, base64 and JSON headers.
  An unexpected actual encoded overflow terminates `item_too_large`, with no
  partial/truncated image and no silent quality/resolution reduction.
- Capture images share the existing32MiB retained-image budget and operation/
  activity retention, not a second unlimited store. Active resources cannot be
  evicted. Old images may expire visibly; unavailable history does not trigger
  a new render or turn replay into a fresh observation.

### 6. Qualified backend ownership and responsiveness

- Prefer a registered view's actual ANARI device/RenderIndex with capture-owned
  camera, renderer, frame and offscreen Image Pipeline. Retain the necessary
  device/world/resources; never modify the source camera/renderer/frame,
  viewport manipulator, selected objects, dimensions, accumulation or pipeline.
  Device-manager caching is not a way to create an isolated device instance.
- A host binding snapshots its actual source renderer and applicable pipeline
  settings, then applies only validated capture quality changes to owned state.
  No silent helide fallback, subtype substitution, ignored quality setting, or
  claim of viewport parity from a matching device name alone is permitted.
- Sharing is conditional on qualification for safe concurrent interactive use
  and scene updates. A binding that stalls the UI, perturbs source accumulation,
  lacks safe lifetime behavior, or cannot meet its policy remains unexposed.
  An explicitly provisioned independent worker/device/index or isolated process
  is an alternative only after the same gates; it is not an automatic fallback.
- The public headless capture module uses values, policy/resource quotes, and
  prepare/start/poll/readback/drain lifecycle results, not raw ANARI handles.
  The host owns device execution/thread affinity. Backend work may run on an
  explicitly permitted worker after immutable handles/state are retained; it
  must not read/mutate the Scene or UI from an arbitrary thread.
- Setup/commit, render submission, polling, mapping/copying, normalization,
  encoding and release all need a bounded/cooperative execution story. Current
  synchronous render-twice and wait-on-restart/destruction behavior is not a
  qualified adapter unchanged. One call per pump or a NO_WAIT flag alone is
  not responsiveness evidence.
- At the portable/qualified acceptance workload, no capture service call may
  block host servicing for more than20ms; UI/host heartbeat gaps must remain
  below100ms over the qualified capture/cancel tests. These are acceptance gates
  on controlled otherwise-idle test workloads, not OS-wide real-time guarantees.
  Backends unable to meet them must offload safely or remain unavailable.
- Capture and legacy image-producing work coordinate exclusive per-view
  ownership; no agent frame reuses a held capture frame/pipeline. Preserve
  legacy render request/result shape and do not cancel another accepted job
  to make room. Busy availability and queue deadlines remain machine-readable.
  The interactive viewport is not halted to satisfy this coordination.

### 7. Progress, supersession, and safe release

- Preserve scene/view/policy revision and synchronization guards before start,
  throughout progressive work, before readback/publication, and on successful
  completion. Immutable camera inputs do not freeze the scene. Relevant scene
  or source-view configuration changes produce `superseded` without an image;
  non-ready scenes use `sync_required`. No historical-state reconstruction.
- Progress distinguishes queued/setup/rendering/readback/encoding/draining
  explanations while retaining existing frozen operation progress states.
  Report completed frames, confirmed samples or explicit unknown, and the
  requested/effective quality unit. Completion is based on qualified backend
  evidence, not number of pumps, render-call invocations, or visual convergence.
- Cancellation, deadline, permission withdrawal, disconnect/expiry and backend
  fault prevent late image success. A logically terminal job may remain
  `awaiting_cleanup`; retain its view/operation/memory reservations and owned
  handles until safe drain/release, indefinitely if the backend cannot confirm
  safety. Never force free, reuse a slot, or claim a backend is dead from age.
- Partial setup failure, submission failure, readback/encoding failure, service
  destruction and shutdown follow the same release contract. No hidden wait
  in a destructor on the servicing thread. Completion before cancellation stays
  completed; ownership/read restrictions still govern historical retrieval.
- Current read/render policy is checked before work and publishing scene-bearing
  results, not cached forever at attachment. Owner minimal receipts remain
  available under existing rules without image/camera/provenance leakage after
  read revocation. Foreign-session history requires all-target current read.
- Capture does not commit scene edits or produce a Batch Reference. Activity
  identifies capture kind, target view/scene, truthful progress/outcome, quality,
  provenance and held cleanup capacity. Existing local Disconnect and lifecycle
  controls continue to work; no chat or approval-dialog dependency is added.

### 8. Results, image retrieval, and outcomes

- Capture success returns its Operation Reference/kind, matched scene/view/policy
  revisions, actual source binding, Device Identifier, renderer subtype/settings
  and pipeline revisions, effective camera/aspect/dimensions, requested/effective
  quality, completed frames, confirmed samples when known, denoise applied,
  unique fresh-capture identity and image-retention state. Unknown optional
  properties are marked unknown, not fabricated.
- Add `vsr_capture_image` as a control retrieval for `operation_ref`, using the
  same all-target read and image-retention contract as existing render images.
  Operation status contains bounded metadata without base64; image retrieval
  supplies owned PNG content only for a retained terminal successful capture.
  Do not accidentally return a legacy render or another operation's image.
- PNG remains RGBA8, sRGB, straight alpha, top-left origin at exactly requested
  dimensions, with source-buffer lifetime detached from device reuse. Reuse
  current normalization/encoding behavior without changing imported-image
  orientation, applying another tone map, or saving a server file.
- Use existing outcomes: `invalid_request` for shape/domain/unsupported request
  choices, `forbidden` for current authority/exposure, `busy` for bounded
  reservations/held capacity, `superseded`, `sync_required`, `cancelled`,
  `timeout`, `item_too_large`, and `outcome_unavailable` for expired results.
  Append headless semantic `UNAVAILABLE`, mapped to `unavailable`, for capture
  configuration/qualified backend absence and confirmed backend failures. The
  proxy already uses this local wire code; it is an explicit additive headless
  outcome here, not an existing enum assumed to be present. Preserve every
  legacy outcome's mapping and numeric identity; no successful fallback.
- Provide structured reasons such as unsupported projection/quality/denoise,
  invalid camera basis/FOV, limit exceeded, unqualified binding, stale policy,
  invalid/unavailable bounds, encoding size, or backend failure. Messages are
  explanatory; clients do not parse English. No private proof/request bodies
  or unbounded free-form callback diagnostics are included.

### 9. Client-side PNG saving

- Extend the trusted stdio proxy with operator opt-in `--save-dir` and local
  `vsr_save_image`, absent from its tool list when saving is disabled. Parameters
  are `operation_ref` and basename `name`, not a directory/path or a command.
  This tool is proxy-local: the server gains no filesystem API or authority.
- Accept one to128 ASCII letters/digits/underscore/hyphen for the basename;
  reject separators, dot components, control characters, absolute paths,
  platform-reserved names and incompatible values, rather than sanitizing them
  into a different collision-prone name. Append `.png`. `checkmatte` therefore
  produces `checkmatte.png`. Initial policy never overwrites an existing target.
- Fetch only a retained successful image under current upstream authentication
  and all-target read. Distinguish legacy render/capture kinds and use the
  corresponding image retrieval. No automatic recapture, old session proof,
  image expiration workaround or plaintext credential exposure.
- Validate and anchor the configured directory and destination with no-follow,
  directory-relative operations. Reject unsafe replacement/redirection,
  symlinks/nonregular existing destinations, and unsupported platforms before
  writing. Same-account malicious-process isolation is not newly promised.
- Bound the complete upstream response and decoded PNG before writing. Validate
  expected PNG MIME/signature/dimensions and source metadata. Create an exclusive
  private temporary file, write the full image, and publish atomically without
  replacement. Clean up only the owned temporary file on failure; no unrelated
  files are read, overwritten, chmod-repaired, scanned or removed.
- Return a bounded save receipt with actual path, source Operation Reference,
  capture/render provenance, byte count and creation status, never credentials.
  Repeated save to an existing destination reports `conflict` with a structured
  destination-exists reason; lost acknowledgement does not justify overwrite.
  I/O/unsupported-directory failures are explicit unavailable outcomes. Local
  saving does not consume a scene request ID or grant operation execution rights.
- Upstream/custom tool-name collision with the reserved local save tool is a
  configuration error, not silent shadowing. Use normal client permission gates
  and truthful file-write annotations. No Lua/shell, archive, raw-byte arbitrary
  write, arbitrary path browsing, or remote transfer capability is added.

## Testing Decisions

### Primary seam and principles

Reuse the already confirmed public headless integration seam with real Scenes,
explicit registered view/bounds/capture bindings and a host-owned safe-point
loop. Controllable public backend adapters are prior art for testing pending,
fault and delayed release, not new private testing hooks. Thin MCP, proxy-local
file-publication and viewer/backend qualification checks cover their genuine
system interfaces without duplicating the business implementation.

Tests check external results, public state, exact main-view invariants,
permissions, operation references, resource/capacity behavior and independently
worked geometry/camera/image oracles. Do not infer correctness from compilation,
GPU availability, poll count, aggregate assertion counts, or a model's aesthetic
judgment. No unguarded live Scene/UI reads from another thread; owning-thread
snapshots/receipts are required by the recent reliability findings.

### Required coverage

1. **View truth:** selected/bound camera versus arbitrary Scene camera, actual
   Device Identifier/subtype and renderer/pipeline provenance, configured agent
   difference, missing binding, policy revisions, hidden targets/read denial,
   stale observations and unchanged legacy discovery/render behavior.
2. **Bounds:** independently worked analytic and host-resident triangle boxes,
   transformed/shared instances, subtree versus scene visibility defaults,
   empty/disabled content, incomplete/unsupported contributors, array writes,
   removal/reuse, stale geometry/ancestry, invalid non-finite boxes, deep/large
   incremental work, cache eviction, cancellation/deadline, scratch accounting,
   and device-only data without hidden download or driver wait.
3. **Camera validation:** both direction and target forms, explicit degrees to
   radians, derived aspect, nonfinite/overflow values, equal eye/target,
   zero/near-parallel basis, FOV boundaries before/after rounding, unsupported
   projection, normalized effective values, and no persistent camera mutation.
4. **Policies and transport:** exact limits and checked overflow arithmetic,
   1280×720/256 and supported full-HD, unsupported quality/denoise, known memory
   reservations, unknown driver estimates reported honestly, encoded PNG and
   whole base64/JSON response bounds, no truncation/clamping, shared retention,
   and normal proxy16MiB protection remaining effective.
5. **Lifecycle:** controlled adapters cover start/readback/setup faults,
   asynchronous progress, confirmed frames/samples, correct accumulator reset,
   stale pre-start rejection, same-ID replay, changed-ID-content conflict,
   cancellation before/after completion, scene/view/policy supersession,
   read/render withdrawal, disconnect/expiry, delayed/indefinite release,
   per-view/global busy admission, shutdown and partial-allocation cleanup.
6. **Nonperturbation:** bit-identical public scene, source camera/manipulator,
   renderer settings, viewport dimensions, source accumulation and unrelated
   revisions across capture/cancel/failure. No queued accepted work is displaced
   to make room. Observer snapshots are captured on the owning thread.
7. **Image correctness:** existing asymmetric orientation/channel/sRGB/straight-
   alpha oracle and retained-buffer lifetime, plus an independently worked
   real-device camera scene with distinguishable coloured objects and explicit
   lighting. Place the camera outside geometry; selected rays/regions must see
   the expected object/background and differ predictably for another camera.
8. **Backend qualification:** real supported devices run setup/pump/readback/
   cleanup heartbeat/latency and measured memory tests at target dimensions and
   quality, including concurrent main-view rendering and scene updates. A
   backend cannot be advertised from a fake adapter test alone. CPU helide
   and the available CUDA-backed selected-view implementation must be tested
   separately; CUDA algorithm parity is not ANARI capture/GUI evidence.
9. **File publication:** opt-in/disabled exposure, correct basename/extension,
   traversals/absolute/control/reserved names, existing files/symlinks,
   unsafe-directory substitution, strict opened-directory/file validation,
   size/MIME/PNG checks, short write/disk failure, atomic no-replace publication,
   owned-temp cleanup, repeated save, expired image/current-permission denial,
   and no credential/private-proof leakage. Server state never changes.
10. **Regression/configuration:** all existing broad-editing identity, grouped
    local/world/mixed batches, exact restoration, bounded-name, session/replay,
    rendering and synchronization regressions. Integration/MCP disabled builds
    remain valid; Lua/UI/Pi and save support are optional. CPU and CUDA builds
    must compile/run the relevant cases with explicit device qualification.

### End-to-end acceptance

- Extend the portable chess scene or an equivalent independently worked fixture:
  query the view and queen/king bounds, derive a world camera above/outside the
  queen looking toward the king, capture1280×720 with samples256 on a qualified
  binding, retrieve the successful PNG, and save `checkmatte.png` through the
  opt-in client helper. Verify file dimensions/content/provenance, immutable
  scene/main-view state and exact source references, with no Lua, source/process
  inspection, asset reload, offline rerender or server path write.
- Repeat through real loopback MCP and the trusted stdio proxy, including async
  operation-status polling and current ownership proof. The image reaches an
  image-capable client as PNG content; automated geometry/image assertions are
  the acceptance oracle, not whether the client finds the picture attractive.
- Run the imported chess viewer workflow under isolated user configuration and
  private authenticated local display. Verify actual device/renderer selection,
  visible responsiveness during capture/cancel, owned-process cleanup, and no
  change to the user's original preferences, scene or screenshot. Missing
  optional asset files do not excuse skipping the portable acceptance.
- Record exact successful commands, case counts and limitations. Install freshly
  changed libraries before tests where installed lookup wins; no CPU fallback,
  success-with-skip, suppressed failure, or absent test match is GPU/capture proof.

## Out of Scope

- Persistent Scene/view/viewport camera edits, main-view movement/reset, camera
  journals/undo, live Lua execution, generic renderer/material/light editing,
  geometry creation/deletion/reparenting, and arbitrary object-reference edits.
- Arbitrary projection models, camera animation, multi-scene registered views,
  historical scene snapshots, automatic convergence or unsupported denoise.
- Silent renderer/device fallback, bit-identical cross-device viewport parity,
  unqualified blocking backend wrappers, forced cancellation/freeing, or a
  guaranteed eventual cleanup of a backend that never reports safe release.
- GPU/proxy array download inside inspection, bulk data export, filesystem
  browsing, server-side file saving, remote/browser-origin attachment, new
  controller/authentication schemes or multiple-controller ownership.
- Durable capture/image journals, cross-restart operation references, save-file
  overwrite/append/delete, arbitrary byte writes, and wire asset transfer.
- Source archive/Application Dump/image-import orientation or Studio Shot
  schema/protocol changes, shared result-store extraction, broad container
  migration, and unrelated architecture or global host-tooling changes.
- Provisioning a GPU CI runner or claiming hosted compile-only CUDA CI proves
  runtime capture. Publishing local specs/tickets/working evidence remotely.

## Further Notes

- User approval covers the recommended coherent increment: actual-view
  inspection, cached scene/node bounds, immutable capture-owned camera/resources,
  qualified use of the registered device/index, and opt-in client-side saving.
  Numeric defaults, tool names, visibility rules, validation and response
  contracts above complete those decisions technically; no capture prototype
  or implementation is claimed by this specification.
- [Proposal](proposal.md) retains source exploration and focused architecture
  advice. The earlier planner stopped before returning its report; parent
  corrections distinguish source facts from unverified suggestions. This spec
  supersedes its open-choice wording for the approved first increment, not
  its honest backend qualification limits.
- [Experiment notes](../vsr-mcp-gaps.md) and
  [offline workaround](../scripts/render_checkmatte.lua) motivate the workflow,
  not a requirement to execute/reload them. [Broad editing](../broad-editing/spec.md)
  supplies the completed placement foundation. Its [progress](../broad-editing/progress.md)
  and [reliability summary](../reliability-verification/SUMMARY.md) record actual
  CUDA, sanitizer, safe snapshot and protocol regression evidence.
- Use Image Pipeline, Image Pass, Device Identifier, ANARI Library and Device
  Subtype in their established rendering sense. Capture is not a Studio Shot
  and the Studio file-ownership ADR is not a new filesystem permission here.
- The public headless test seam was already confirmed and retained; no new
  private seam or additional testing interview is needed for this synthesis.
  Device/frame/pipeline qualification remains implementation acceptance, not
  an excuse to advertise every backend after one CUDA kernel test passes.
- `ready-for-agent` means specified for implementation, not implemented,
  runtime-qualified or independently panel-reviewed. No tickets or camera/
  capture code are created in this specification step; all material is local.
