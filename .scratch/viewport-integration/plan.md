# Draft plan: truthful views, presented frames and safe selection movement

**Status:** draft — planning only; scope decisions below need confirmation.

## Goals and evidence

Deliver the four requested capabilities:
1. Truthful registered-view identity and rendering provenance.
2. Retrieval of the latest displayed viewport image, without rendering a replacement.
3. Easier correct request construction and actionable diagnostics.
4. Explicit transform semantics/examples first, then versioned selection translation with preview.

Live validation found a barney viewport displaying a1005x217 render buffer at1340x290, while the sole MCP view rendered an independent640x360 Helide preview and reported missing binding. Running/installed/build-tree viewer binaries matched. Native inspection schema requires only request_id/scene_id; no standard Pi optional-forcing bug was demonstrated. Omission-based protocol fixtures and isolated edit/restore tests pass, but a legal live Queen_B request remains to be verified through an omission-preserving client path. See ../viewport-validation/report.md and ../viewport-frame-handoff.md.

This plan does not implement source, publish tickets, modify frozen specs/addenda, change user configuration, or request a running-viewer restart.

## Compatibility boundaries

- Keep the current8MiB PNG,12MiB complete response,16MiB proxy,2048axis and2073600pixel capture/observation defaults for this increment. Do not silently resize or increase limits. Larger8K-plus output remains future coordinated policy/transport/resource work, not a permanent encoder prohibition.
- Preserve legacyvsr_render argument shape, dimensions, grants, retained images and headless behavior; add truthful provenance rather than silently turning it into viewport capture or disabling it because new capture is unexposed.
- Keep actual view observation, latest-presented image retrieval, and fresh immutable capture as distinct contracts. Existing camera-capture08 is fresh capture; it does not automatically fulfill presented-frame retrieval.
- Preserve strict kind-specific inspection checks, meaningful empty names and omitted optional filters. Never fix malformed requests by inventing zero handles or stripping every empty value.
- Preserve existing atomic edits, current permissions, exposure, version/ancestry history, cancellation, activity and conditional one-way restore. No chess-specific rules or inferred piece ownership.
- No renderer/camera/viewport state mutation to read a frame, no Helide fallback, no direct UI state reads on transport threads, no unbounded GPU download or Scene walk.

## Recommended public semantics

### Views and provenance

Reusevsr_view_info and authorized discovery. Expose a stable view identity, bounded display name and an explicit origin/role. Include interactive_viewport and independent_agent, while retaining a truthful generic/unknown form for existing headless/custom hosts rather than forcing every host into those two categories.

Distinguish currently effective binding/configuration from the configuration that produced an image. Report the actual Device Identifier/subtype, genuinely bound camera/renderer, native render dimensions, displayed dimensions/scale, applicable Image Pipeline/color settings and matched revisions. Unknown information carries reasons; no rendered revision is fabricated when no frame exists. Inspecting metadata allocates no renderer or GPU readback.

Device switch, camera/renderer change, resize, pipeline changes, closure/recreation and exposure changes invalidate appropriate state. Recreated views cannot alias retired identities. Discovery/render/image receipts must identify an independent agent image as such even when full viewport metadata is unavailable.

### Latest displayed frame — proposed default

Return the viewport's final display-ready image, including enabled in-pipeline effects/outlines, excluding ImGui chrome, HUD, gizmos and unrelated desktop pixels. This is not an OS screenshot. Document output color conversion/orientation and native-versus-displayed scaling; do not imply bit-identical monitor/compositor output from pipeline metadata alone.

A frame must be associated with an actual successful viewport display submission, not merely a pipeline buffer that was produced or uploaded but never shown. Capture its rendering provenance at the appropriate submission/completion boundaries, not by relabeling it with whatever camera/Scene settings happen to be current at retrieval.

Define latest relative to the request's owning-thread observation point. Pin that presented frame and provide its immutable owned snapshot, possibly after bounded asynchronous readback. If it is not safely available, return pending/unavailable; never label an older completed readback as the newest presented frame. Report returned frame identity/timestamp, the observed presentation identity, rendered scene/view/configuration revisions and separately observed current revisions. The response can become historical while it is delivered; that is explicit, not an error or silent re-render.

If the displayed frame depicts an older Scene state, return that frame marked stale relative to current state, with both revision sets. This fulfills 'show what is displayed', not 'render current state'. After resize, retained frame dimensions remain those actually presented. Device change/closure/recreation invalidate latest-frame availability; any permitted historical retained result remains explicitly historical and subject to current authority.

Enable snapshot production only when the host exposes a qualified frame source and an authorized observer is active. A cold cache may report not_yet_available until a naturally produced/presented frame is observable; do not force a render. Current Scene/view read checks and explicit host frame exposure govern delivery and retrieval; capture/render exposure is not inferred from metadata. Final permission rules are recorded in the additive spec before exposure.

### Selection translation with preview — proposed default

Expose a preview tool and a separate commit tool, names provisional:vsr_translate_preview andvsr_translate_commit. Both use one Translation module sharing actual edit preparation/validation; no independent approximate simulator.

Preview accepts one Scene, a bounded selection of node handles with inspection's node-edit/structural/ancestry expectations, and a finite world-space translation delta. Normalize to the topmost **selected** roots. Selected descendants inherit once; never guess an upward/common piece parent that could move unselected siblings. Only eligible scalar-transform roots are translated; no node creation or wrapper insertion. Valid selected descendants that are inherited do not become separate position writes.

Preserve each root's linear transform and move its existing local origin by the requested world delta using the established world-position conversion through its parent's affine transform. Rotation, shear, reflection and local zero scales retain their supported semantics; a singular required parent, nonfinite computation or float32 overflow rejects the whole operation. Use current edit conversion/rounding rules; ordinary values such as0.1 must not be rejected solely for not round-tripping exactly between double and float32. This is origin translation, not geometry-centre movement.

Preview returns normalized roots, selected nodes moved by inheritance, predicted current/final origins and prepared matrices, consumed dependency expectations and bounded warnings. Unselected descendants also inherit a selected root's movement. Do not claim a complete affected-descendant list/count unless it was actually computed within limits; an unenumerated/truncated set is explicit. No complete-Scene scan, commit or restore record is created by preview.

Commit references that exact owned prepared preview, rechecks all required identities, relevant versions/topology/ancestry, current permissions and exposure, and commits atomically through the existing edit path. It must fail stale rather than silently rebase or change root normalization. Reuse normal edit/restore/activity/replay/cancellation semantics. Current precise dependencies—not a broad arbitrary Scene counter alone—decide whether unrelated edits actually conflict.

Preview storage belongs to the edit/translation module, keyed by existing session/launch identity with bounded count/bytes/expiry/global capacity and cleanup. SessionAuthority remains about authentication/grants, not Scene matrices or prepared edit plans. No global map migration/shared-result-store refactor. A successful commit consumes the preview; replay of the same admitted request returns the historical outcome without applying it again.

## Staged delivery and genuine dependencies

### 0. Align the intended development base

Inspect current Jujutsu history/status without overwriting user.envrc/.pi/mcp.json. The working copy used for live validation is an older snapshot; a bookmark label or WIP subject alone does not establish which accepted code is present. Identify a lineage containing the needed integration, versioned edits, qualified encoder and verified scene-dependency/ownership work, including the Dataset test correction. Do not start from baretrunk if that drops these prerequisites. Record remaining03 acceptance state explicitly.

Choose the actual work revision/workspace with user-owned changes preserved, then reuse the appropriate existing enabled build/install or an explicitly approved workspace setup. Establish green baseline and executable/descriptor provenance. No reset, rebase, recovery of unrelated workspaces or running-viewer restart is implied by this draft.

### A. Deliver knowledge and actionable request diagnostics first

Add concise minimal examples for object/layer/node queries and transform inheritance/world-origin semantics to tool descriptions and response guidance. Put a longer placement guide in a resource only if the client demonstrably exposes/fetches it; descriptions/structured results are primary.

Return bounded structured invalid-request diagnostics containing known argument field, kind and reason rather than onlyinvalid_request. Validate syntax/inapplicable filters before admission; any foreign/live-identity assessment follows current authority and safe-point rules. Echo no secrets, raw Scene state or arbitrary large user values. Preserve request-ID/replay behavior.

Measure actual client/model-facing schema and transported argument shapes before changing optional handling. Prefer omission-preserving fixes at a demonstrated faulty layer; a narrower node/object/layer tool surface is optional if flat mixed-kind discoverability remains poor. Do not assume every provider accepts structuredoneOf. Unknown-property strictness changes need compatibility review, not incidental blanket rejection on the legacy tool.

Acceptance:valid minimal/omitted queries, meaningful empty-name queries, exact bad-field diagnostics, malformed/mixed-kind/placeholder-handle rejection, and correct inspect→versioned edit→reinspect→restore on isolated public tests. Positive matched cases matter; existing97/133assertion totals are historical evidence, not frozen counts that prevent adding tests. Close the legal liveQueen_B/pawn inspection gap without taking over the existing controller. No live chess edits until explicitly authorized.

### B. Deliver truthful view identities and read-only bindings

Reuse camera-capture20(policy observation) and14(real/agent binding observation), adjusting the task mapping only after identifying what is actually complete on the chosen base. Add role/name provenance without editing the frozen parent spec. Observe owning-thread genuinely bound camera/renderer and pipeline state, including switching/teardown unknowns; separate interactive viewport from legacyagent. Preserve headless/custom hosts and legacy render compatibility.

Acceptance:authorized discovery distinguishes views; effective view info tracks actual binding, not an unrelated camera. Metadata-only queries perform no renderer creation/readback or state change. Validate lifecycle, revision coherence and authority through the headless interface and genuine viewer interface; perform a real livebarney check with source/frame dimensions reported, not hardcoded to the historical1340x290 snapshot.

A and B are logically independent but may touch shared MCP files. Parallel implementation requires proven disjoint ownership; otherwise keep writers serial.

### C. Deliver presented-frame observation end to end

Build a deep Frame Observation module with an owning-thread producer and an immutable bounded consumer, plus the viewer adapter, admitted MCP observation and image retrieval path. Rendering-local storage stays Scene/integration-token independent; the host adapter supplies matched public provenance without upward library dependencies.

Trace ImagePipeline's final color buffer, output/color transform, CUDA/CPU ownership, CopyToSDLTexturePass and actual UI presentation submission before selecting the observation hook. A rawgetColorBuffer pointer is not an owned snapshot or proof of presentation. CPU copying, GPU submission/event/readback/encoding/release and source reuse all need qualification. Never assume an async CUDA call or event query is inherently bounded.

Bound staging slots, immutable snapshots held by consumers, PNG/base64/complete responses, retained results and aggregate host/device bytes—not just the number of staging buffers. Slow consumers cannot cause unbounded copies or block/reset live accumulation. Cancellation/teardown drains any submitted work safely; read withdrawal suppresses images/metadata at work, publication and retrieval. Reuse the qualified PNG encoder and existing image/operation semantics; add a frame-specific retrieval tool if legacy render-image retrieval cannot accept a new operation kind without changing its contract.

Acceptance:headless registered real pipeline source and viewer/MCP paths demonstrate no extra render or persistent state change, last-presented/request-bound frame identity, stale/current revision distinction, cold cache, resize/scale, device changes, close/recreate, denied access, expiry/replay, cancellation and resource exhaustion. Qualify relevant CPU and currentbarney/CUDA paths with real latency/heartbeat/memory evidence; an unsupported GPU-only source stays unavailable. Mock-only or CPU-only tests do not complete the livebarney goal.

C depends on B and its host limits/authority/binding contract. Fresh immutable capture08 remains a separate capability, not an implementation prerequisite unless actual source/resource ownership requires an explicitly justified dependency.

### D. Deliver versioned translation preview

Reuse accepted node identity/edit/ancestry and ownership/invalidation prerequisites. Factor a common prepared-translation path in the edit module; implement non-mutating preview with bounded prepared-plan retention and the knowledge/diagnostic surfaces from A. Keep low-levelvsr_edit available.

Acceptance:parent+child selection moves once in predictions; independent roots, sibling parts, rotated/sheared/reflected parents, singular/overflow inputs, ineligible roots, duplicates/foreign/stale handles, permissions/exposure changes, bounded ancestry and preview eviction are tested through the public headless/MCP interface. Preview does not mutate Scene/revisions or create restore eligibility. Root normalization never silently broadens selection upward.

### E. Deliver atomic translation commit and restoration

Consume a prepared preview with the existing edit engine. Verify all selection/root-normalization dependencies, not merely the final root handle, so hierarchy changes cannot silently alter which nodes inherit. Current read/edit/exposure checks remain mandatory; failed validation leaves the entire Scene unchanged. Preserve replay and one-way conditional restore rules, precise version changes and cancellation boundaries.

Acceptance:preview→commit→reinspection→restore proves once-only world displacement and exact restoration; intervening relevant changes conflict, permitted unrelated changes follow existing narrow-dependency semantics, and stale/consumed/expired previews cannot reapply. Full regression/memory verification and independent review accompany completion. Live chess smoke uses fresh inspected ancestry/coordinates and explicit user edit/restore permission—no legal-chess inference or offline placement guess.

D depends on A and the existing verified edit/identity foundation, not on displayed-frame work. E depends on D. B/C and D/E can be independent design workstreams; shared-file writers remain serial unless separated safely.

## Existing work and new specification material

- Preserve immutable camera-capture/broad-editing/scene-invalidation specs and existing addenda. Any authorized extension is a **new** addendum file or a new focused spec referencing them, never a new section silently inserted into a frozen spec.
- View identity/provenance extends the existing20/14 contracts. Presented Frame Observation is new additive behavior, distinct from08freshcapture.
- Request guidance/diagnostics extends inspection without relaxing current semantic validation.
- Selection Translation is new additive behavior on existing edit/restore contracts; separately specify preview lifetime/limits and exact commit/replay behavior before coding.
- Do not automatically resolve old tracker parents, recreate completed prerequisites or convert this draft into implementation-ready tickets without approval.

## Decisions to confirm before specification/tickets

1. **Image scope:** recommend final display-ready pipeline image, including in-pipeline effects, excluding UI chrome/HUD/gizmos. If those UI overlays are required, plan a separate viewport-composition capture seam—not an OS desktop screenshot.
2. **Historical display:** recommend returning the actual request-bound displayed frame even if current Scene state is newer, clearly labeled with both revision sets. Never silently render current state instead.
3. **Observation activation:** recommend qualified caching only while an authorized observer is active; cold-cache/pending behavior is explicit. Always-on caching needs a separately accepted background cost.
4. **Translation shape:** recommend a dedicated preview→commit pair with a bounded exact prepared plan, rather than expanding legacyvsr_edit with ambiguous preview semantics. This is an additive interface, not a change to low-level edit/restore behavior.

## Verification and release discipline

Prefer the already confirmed public headless host/MCP seam with real Scene/ImagePipeline sources plus genuine viewer presentation tests. Avoid separate private schemas/simulators that bypass real behavior. Build and install the chosen enabled configuration before runtime tests; discover actual authoritative recipes rather than guessing build flags/targets. Full test/memory evidence and independent source/spec/Standards review are required, with older-trunk coverage limits stated honestly.

No viewport restart, live edits, source implementation, cap increase, new ticket publication or claim of pixel parity/quality/8K capability follows from this draft. The next step is confirming the four defaults, then writing the additive contracts and approved dependency-aware execution plan.
