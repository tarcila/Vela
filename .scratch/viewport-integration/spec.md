# Truthful Views, Presented Frames and Safe Selection Translation

**Status:** ready-for-agent
**Milestone:** 1

## Problem Statement

An agent asked to show what the user is seeing can currently receive a separately rendered image of the same Scene instead of the interactive viewport's output. Live validation showed a barney viewport with an HDRI environment and a different aspect ratio, while the sole registered MCP view returned a fixed-size independent Helide preview. That view was renderable but its effective binding observation was unavailable. The returned image did not expose enough provenance to prevent a caller from treating it as a viewport frame.

The agent can also construct invalid inspection requests by filling every optional field, mixing object filters with node queries, or supplying placeholder handles. Those failures do not establish a broken scene-editing implementation. Native omission-based inspection and edit/restore protocol tests pass; the model-facing request path still needs clear examples, actual omission-preserving validation and actionable rejection information.

Finally, correct placement editing requires transform-inheritance knowledge. Applying the same local displacement to both a selected ancestor and its selected descendant can translate the descendant twice. The caller should not have to reconstruct an assembly's intent from names or remember every coordinate-space rule before making an ordinary selection move.

## Solution

Expose truthful identity and provenance for registered interactive, independent-agent and other host views. Retain legacy independent rendering, but label it accurately and distinguish it from observing the viewport.

Add read-only retrieval of the viewport image most recently submitted for display at the request's owning-thread observation point. Return the actual image with its frame identity, rendering provenance and dimensions; never render a substitute. A displayed frame can represent older Scene state, so report both rendered and currently observed revisions honestly.

Make valid inspection requests easy to construct through concise kind-specific examples, explicit optional/filter semantics and bounded diagnostics for invalid arguments. Keep strict validation and existing accepted request behavior.

Deliver transform semantics and worked examples before introducing a versioned selection-translation preview and dedicated commit operation. Normalize only the topmost selected roots, predict once-only world-space movement using the actual edit preparation machinery, and commit the exact prepared plan atomically with current dependency and permission checks. Existing low-level edit and conditional restoration remain available.

## User Stories

1. As an agent, I want each registered view to identify its role, so that I know whether an image represents an interactive viewport or an independent preview.
2. As a user, I want an independent agent rendering labeled honestly, so that it is not presented as the image I am seeing.
3. As an agent, I want stable view identities and bounded names, so that I can select a specific viewport without guessing.
4. As a host developer, I want generic and unknown view roles supported, so that headless/custom hosts are not falsely classified as interactive viewports.
5. As an agent, I want the actual Device Identifier and subtype reported, so that visual differences are not misdiagnosed as material defects.
6. As an agent, I want the genuinely bound camera identified, so that an unrelated Scene camera is not mistaken for the viewing camera.
7. As an agent, I want effective camera projection and pose described with explicit conventions, so that coordinate and FOV interpretation is reliable.
8. As an agent, I want the bound renderer and applicable settings reported, so that the source image's rendering configuration is clear.
9. As an agent, I want Image Pipeline and output-color provenance, so that I can distinguish rendering from presentation effects.
10. As an agent, I want native render-buffer dimensions distinguished from displayed viewport dimensions, so that scaling is not hidden.
11. As an agent, I want effective configuration distinguished from image-producing configuration, so that a changed UI selection does not relabel an old frame.
12. As a host developer, I want unknown metadata to carry explicit reasons, so that a complete binding is never fabricated.
13. As a user, I want metadata inspection to create no renderer or readback, so that inspecting provenance does not disturb viewing.
14. As an agent, I want device-switch transitions reported honestly, so that an old device is not falsely described as the newly selected one.
15. As an agent, I want camera and renderer changes to invalidate the appropriate observations, so that stale configuration is detectable.
16. As an agent, I want resize and pipeline changes to invalidate their observations, so that dimensions and effects remain trustworthy.
17. As an agent, I want viewport closure and recreation to retire old identities, so that a new viewport cannot masquerade as a previous one.
18. As a user, I want legacy render behavior preserved, so that existing headless and independent-preview workflows keep working.
19. As a host developer, I want source roles independent of permissions and qualification, so that labeling a viewport cannot enable image access.
20. As an agent, I want discovery and image results to preserve source identity, so that an image cannot lose its origin between operations.
21. As a user, I want unsupported bindings reported instead of substituted, so that there is no silent Helide fallback.
22. As a maintainer, I want build and registration provenance checked before diagnosis, so that a mismatched executable is not confused with a host-binding defect.
23. As a user, I want an agent to retrieve the image currently displayed by my viewport, so that we discuss the same composition.
24. As a user, I want frame retrieval to trigger no new render, so that it does not reset accumulation or replace the displayed image.
25. As an agent, I want the final display-ready pipeline image, so that applicable pipeline effects are represented.
26. As an agent, I want in-pipeline outlines distinguished from UI overlays, so that inclusion of each is explicit.
27. As a user, I want unrelated desktop pixels excluded, so that a scene observation is not an uncontrolled desktop screenshot.
28. As an agent, I want top-left image orientation and color/alpha provenance, so that the returned image is not flipped or transformed twice.
29. As an agent, I want a frame identity and monotonic timestamp, so that I can refer to the image actually observed.
30. As an agent, I want latest defined at a specific request observation point, so that frame delivery latency does not make the result misleading.
31. As an agent, I want the frame's rendered revisions preserved, so that current Scene changes do not rewrite its history.
32. As an agent, I want currently observed revisions alongside rendered revisions, so that I can identify a displayed image of older state.
33. As a user, I want an older displayed frame returned with an explicit stale indication, so that the server shows what is displayed rather than silently rendering current state.
34. As an agent, I want an unfinished readback reported as pending or unavailable, so that an older copy is not passed off as the latest frame.
35. As a host developer, I want a successful display-submission association, so that produced-but-never-shown buffers are not called presented frames.
36. As a user, I want old dimensions preserved on retained frames after resize, so that their metadata still describes their pixels.
37. As an agent, I want native output and display scale reported without hidden resampling, so that returned resolution has a clear meaning.
38. As a user, I want device changes and viewport teardown to invalidate latest-frame availability, so that old sources cannot be silently revived.
39. As an agent, I want multiple viewports selected explicitly, so that active-window guessing cannot return the wrong image.
40. As a user, I want observation enabled only for a qualified host source and authorized observer, so that inactive observation adds no background copying.
41. As an agent, I want cold-cache behavior documented, so that not-yet-available is not mistaken for empty geometry.
42. As a user, I want live rendering to continue while frames are observed, so that image retrieval cannot halt or reset my viewport.
43. As a host developer, I want owned immutable snapshots, so that a mutable pipeline pointer cannot change underneath delivery.
44. As a host developer, I want CPU and GPU ownership handled explicitly, so that neither storage type is assumed readable or stable.
45. As a host developer, I want bounded asynchronous readback and cleanup, so that a nominally nonblocking call cannot hide a long driver wait.
46. As a host developer, I want staging and retained snapshots bounded together, so that slow consumers cannot accumulate unbounded memory.
47. As an agent, I want oversized images rejected with actual dimensions and limits, so that no undocumented clamp or scale changes the output.
48. As a user, I want current image limits retained for this increment, so that viewport observation does not silently raise transport or memory ceilings.
49. As a host developer, I want PNG, base64 and complete-response overhead checked, so that an encoded image cannot exceed the carrier budget.
50. As an agent, I want admitted frame operations and polling through the established workflow, so that delivery has familiar identity and deadlines.
51. As an agent, I want retained frame replay to stay historical, so that repeating a request does not fetch a different image under the same identity.
52. As a user, I want read withdrawal checked during work and retrieval, so that an old image reference cannot bypass current permissions.
53. As a user, I want cancellation and teardown to drain owned work safely, so that in-flight readback cannot use freed storage.
54. As a host developer, I want unavailable reasons to distinguish missing, unqualified, pending and expired sources, so that clients can respond correctly.
55. As an agent, I want empty or incomplete frame state distinguished from unavailable geometry, so that a missing frame does not imply an empty Scene.
56. As a user, I want viewport observation separate from fresh one-off capture, so that observing does not unexpectedly change rendering work.
57. As a maintainer, I want real viewport comparisons and responsiveness evidence, so that a mock adapter cannot falsely establish visual parity.
58. As a user, I want future larger rendering possible through explicit policy evolution, so that current defaults do not become permanent product ceilings.
59. As an agent, I want minimal kind-specific inspection examples, so that I can query a node without filling unrelated fields.
60. As an agent, I want unused filters omitted rather than guessed, so that placeholder values cannot accidentally constrain or invalidate the query.
61. As an agent, I want an empty name distinguished from no name filter, so that meaningful unnamed-node queries remain possible.
62. As an agent, I want filter intersection semantics explicit, so that a parent handle and child name are not mistaken for a traversal command.
63. As an agent, I want handles obtained from fresh inspection, so that zero or invented handles are never treated as shortcuts.
64. As an agent, I want invalid arguments identified by field and reason, so that I do not repeat near-identical rejected requests.
65. As an agent, I want kind-inapplicable filters rejected clearly, so that object filters are not supplied to node queries.
66. As a user, I want diagnostics bounded and privacy-preserving, so that rejection does not reveal hidden Scene data or credentials.
67. As an agent, I want syntax errors rejected before admission without consuming request identity, so that I can correct a malformed request safely.
68. As a maintainer, I want live identity assessment to respect authority and safe points, so that helpful errors do not create a side channel.
69. As a user, I want existing valid inspection behavior retained, so that diagnostics are not an excuse for incompatible validation changes.
70. As a maintainer, I want actual client argument transport tested, so that source schema declarations are not mistaken for proof of model-facing behavior.
71. As an agent, I want transform inheritance explained where I call tools, so that I know a parent movement affects descendants.
72. As an agent, I want world position defined as a node origin, so that I do not confuse it with geometric bounds or centre.
73. As an agent, I want a parent-and-child translation example, so that I understand why identical local deltas can double movement.
74. As a maintainer, I want optional narrower inspection surfaces considered only with evidence, so that client compatibility is not changed for an unproven defect.
75. As an agent, I want to preview a world-space selection translation, so that I can verify the intended result before modifying the Scene.
76. As an agent, I want preview and commit to share real edit preparation, so that the prediction is not a separate approximation.
77. As an agent, I want one explicit Scene and fresh dependency expectations, so that the move has a versioned target rather than a guessed placement.
78. As an agent, I want selected ancestors to subsume selected descendants, so that each selected part moves once in world space.
79. As a user, I want only topmost selected roots normalized, so that the server does not choose an unselected common parent.
80. As a user, I want sibling parts translated independently when their parent was not selected, so that unselected siblings do not move accidentally.
81. As an agent, I want inherited selected nodes reported, so that I understand why they were not edited directly.
82. As an agent, I want unselected descendant effects disclosed, so that normal transform inheritance is not concealed.
83. As an agent, I want incomplete affected-descendant enumeration labeled, so that a bounded report is not mistaken for complete impact analysis.
84. As a user, I want duplicate selection entries handled deterministically, so that they cannot cause duplicate displacement.
85. As an agent, I want only eligible scalar-transform roots moved, so that unsupported placements are not silently wrapped or promoted.
86. As a user, I want translation to preserve rotation, scale, reflection and shear, so that a simple move does not alter shape.
87. As an agent, I want world deltas converted through the parent transform, so that rotated or sheared parents do not change the requested direction.
88. As an agent, I want local zero-scale support retained where the parent conversion is valid, so that unrelated scale singularity does not ban an origin move.
89. As an agent, I want singular required parents rejected without repair, so that the server does not invent a pseudoinverse placement.
90. As an agent, I want nonfinite and overflowing values rejected atomically, so that invalid arithmetic cannot partially move a selection.
91. As a user, I want ordinary fractional inputs handled by the existing rounding contract, so that a value such as 0.1 is not rejected for an impossible exact binary round trip.
92. As an agent, I want current and predicted origins and prepared matrices, so that the preview explains exactly what will change.
93. As a user, I want preview to create no Scene write or restore eligibility, so that inspection itself is non-mutating.
94. As a host developer, I want previews bounded by count, bytes and lifetime, so that abandoned plans cannot grow without limit.
95. As a user, I want preview contents governed by current read/edit permissions and exposure, so that preparation cannot bypass authority.
96. As an agent, I want commit to reference the exact prepared preview, so that the server cannot silently recompute a different plan.
97. As a user, I want identity, topology and ancestry rechecked before commit, so that a changed hierarchy cannot alter root normalization silently.
98. As an agent, I want stale previews rejected rather than rebased, so that approval of one result cannot authorize another result.
99. As a user, I want a selection committed all-or-nothing, so that any invalid root leaves every root unchanged.
100. As an agent, I want relevant dependencies used instead of blanket conflicts on unrelated changes, so that independent editing remains useful.
101. As an agent, I want current exposure and permissions checked again at commit, so that a prepared plan cannot outlive authorization.
102. As a user, I want successful previews consumed once, so that a different request cannot apply the same displacement twice.
103. As an agent, I want retry replay to return the same historical commit outcome, so that transport retries do not repeat the move.
104. As an agent, I want existing edit receipts and Batch References, so that translation integrates with established activity and restoration.
105. As a user, I want exact conditional restoration of translated matrices, so that I can recover the prior placement under the existing conflict rules.
106. As a user, I want cancellation boundaries preserved, so that cancellation before commit prevents writes and cancellation after commit is not disguised rollback.
107. As a host developer, I want prepared Scene edits outside the authentication module, so that session identity does not become an edit-plan store.
108. As an agent, I want expired, evicted or foreign previews rejected clearly, so that a token cannot revive removed state or cross a launch/session boundary.
109. As a maintainer, I want public headless tests with real Scenes and pipelines, so that validation crosses the same interface as clients.
110. As a maintainer, I want genuine viewer presentation tests, so that successful buffer production is not confused with displayed output.
111. As a maintainer, I want camera/device/renderer/resize/pipeline lifecycle coverage, so that provenance survives real viewing changes.
112. As a maintainer, I want source lifetime and slow-consumer memory tests, so that cached pointers and held snapshots cannot leak or dangle.
113. As a maintainer, I want real CPU and GPU responsiveness qualification, so that a successful compile does not enable an unsafe source.
114. As a maintainer, I want a native omission-preserving live inspection smoke, so that malformed agent requests cannot masquerade as backend defects.
115. As a maintainer, I want preview/commit/reinspect/restore tested end to end, so that higher-level selection semantics remain correct across transport.
116. As a user, I want live edit smoke tests gated by explicit permission and restoration, so that validation does not rearrange my Scene unexpectedly.
117. As a maintainer, I want the intended revision and build established before coding, so that later accepted work is not assumed present in an older checkout.
118. As a user, I want local configuration and unrelated workspace changes preserved, so that new integration work does not discard my state.
119. As a maintainer, I want exact test, memory and coverage evidence, so that partial verification is not called complete qualification.
120. As a user, I want additive contracts with no hidden fallback or scope expansion, so that observing and moving a Scene does only what I requested.

## Implementation Decisions

1. **Document defaults and delivery order.** This synthesis uses the plan's recommended defaults: display-ready pipeline image without UI chrome/HUD/gizmos; displayed historical state returned with explicit rendered/current revisions; qualified snapshot production only while an authorized observer is active; and dedicated preview/commit translation tools. Deliver semantics/examples and request diagnostics before the new mutating translation operation. View provenance and translation are separate workstreams sharing the existing integration interface.

2. **Additive compatibility.** Reuse authorized discovery and vsr_view_info rather than adding a competing metadata interface. Preserve legacy vsr_render, its view-owned dimensions, permissions, image retrieval and replay. Add source role/provenance without turning legacy independent rendering into viewport observation. Presented-frame observation and fresh one-off capture remain distinct. Capture-policy exposure does not automatically enable frame observation, and its absence does not automatically disable legacy rendering.

3. **Role and identity.** Expose stable view identity, bounded name and explicit role/origin. Interactive viewport and independent agent roles must be distinguishable; generic/headless and unknown cases retain truthful forms. Roles are descriptive, not grants or backend qualification. Retired/recreated views do not reuse an identity that makes old references current. AgentView's camera, Device Identifier and Image Pipeline are independent unless an actual host binding establishes otherwise.

4. **Effective and rendered provenance.** Snapshot genuinely bound camera/renderer, Device Identifier/subtype, projection/pose conventions, native and displayed dimensions/scale, applicable renderer and Image Pipeline settings, output conversion and matched scene/view/configuration/policy revisions on the owning thread. Currently selected objects are not proof of the effective ANARI binding. Image provenance describes the configuration that produced its pixels; current metadata does not relabel an older image. Missing or bounded-incomplete fields carry reasons, not fabricated completeness. Metadata inspection performs no renderer creation, GPU query/readback, device selection, camera reset or Scene mutation.

5. **Presentation meaning.** A presented frame is a completed viewport image associated with a successful submission to that viewport's display path. It is not merely an ANARI frame, a mutable color-buffer pointer, a texture upload that was never submitted, or physical monitor scanout. Include final applicable Image Pipeline effects and in-pipeline Outlines; exclude ImGui chrome, HUD, gizmos, desktop pixels and unrelated windows. Document monitor/compositor effects outside this contract rather than claiming OS-screenshot parity.

6. **Request-bound latest semantics.** At the admitted request's owning-thread observation point, identify the most recently presented frame for the explicitly selected view. Pin its immutable snapshot or bounded in-flight acquisition. Return that exact identity, or pending/unavailable if it cannot safely be acquired. A completed readback of an earlier frame cannot silently stand in for the latest presented frame. Return observation/presentation identity and monotonic timing, plus the frame's rendered revisions and separately observed current revisions. Delivery can be historical because viewing continues; latest is not redefined at response arrival.

7. **Historical display and lifecycle.** Return an actually displayed frame whose rendered state is older than current state, marked stale with both revision sets. Do not render current state to replace it. Retained frames keep their original native/display sizes and provenance after resize. Device change, closure/recreation and source withdrawal retire latest availability; any separately retained historical operation result remains historical and subject to current authority. Cold cache, inactive presentation, pending readback and unqualified source states have explicit reasons. No-frame availability does not imply empty geometry.

8. **Frame interfaces and image delivery.** Add admitted vsr_view_frame with request identity and explicit view identity, using normal status/cancel/replay behavior. Its result identifies the request-bound presented frame and its metadata. Add frame-specific image retrieval through vsr_view_frame_image using the operation reference and the existing qualified normalization/PNG delivery primitives. Do not broaden legacy render-image operation-kind behavior silently. No server filesystem authority or default image saving is introduced. A view without an exposed, qualified presented-frame source returns unavailable rather than an independent render.

9. **Observation authority and activation.** A host explicitly exposes a frame source only after qualification; defaults remain unexposed. Reuse current registered-Scene and view read authority at admission, execution, publication and retrieval, including historical image references. Fetching an existing presented image does not borrow edit permission or invoke render work; no new authentication mechanism or private launch-descriptor schema is introduced. Snapshot production is active only for a qualified source with an authorized observer. When no authorized observer remains, stop additional observation work and reclaim owned resources safely. Cold activation may await natural presentation; it must not force rendering. After read withdrawal, owner minimal receipts carry no protected image/provenance payload.

10. **Deep Frame Observation module.** Use an owning-thread producer and an immutable bounded consumer at the actual presentation seam. Rendering-local storage remains independent of integration wire identities and UI controllers; the host adapter attaches matched public provenance without upward library dependencies. A real viewer adapter and a headless real-pipeline presentation adapter exercise the same consumer contract. Transport threads never directly read live UI, Scene or mutable pipeline storage.

11. **Qualified CPU/GPU acquisition.** Establish buffer storage, stream ownership, presentation association, source reuse, output transform, readback, encoding and release behavior before exposing an adapter. CPU-resident data may be copied into owned snapshots; GPU-only data requires qualified bounded submission/poll/readback/drain or reports unavailable. An async API, event query or nominal NO_WAIT mode alone is not responsiveness proof. Live rendering continues; observation does not cancel its work, reset accumulation, resize it or allocate a fallback device. Resource-induced delay is bounded and reported honestly.

12. **Image/resource limits.** Retain defaults of 2048 per axis, 2,073,600 pixels, 8 MiB encoded PNG, 12 MiB complete response and the existing 16 MiB proxy ceiling. Use checked native-byte, staging, normalization, PNG, base64 and JSON arithmetic before admission/allocation; encoded-size compatibility uses the proved encoder bound. Oversize results report actual source dimensions and the limiting policy without hidden clamp/downsample. Host-qualified sources have finite validated count/deadline and host/device budgets, initially no greater than the existing 256 MiB owned-resource ceilings. These are observation-owned resource budgets, not claims about all Scene/renderer memory. Account for staging, current snapshots, consumers retaining older snapshots, image results and cleanup across views; a staging-slot bound alone is insufficient. Busy/exhaustion must not stall or reset the live viewport.

13. **Request knowledge and examples.** Tool descriptions and structured responses are the primary knowledge-delivery path. Include minimal kind-specific request examples, omission versus meaningful empty-filter semantics, filter intersections, fresh-handle/version requirements and inherited-transform/world-origin rules. A longer resource or prompt guide is supplemental only when the client actually exposes or fetches it. A parent-and-child example states that one selected parent moves both origins once through inheritance; applying identical local deltas separately can move the child twice. A sibling example states that selecting two sibling roots does not authorize selecting their common parent. No legal-chess or name-based piece inference is added.

14. **Bounded diagnostics without weakened validation.** Preserve stable outcome codes and already accepted inspection semantics. For rejected requests, add bounded structured details naming known argument fields, applicable kind and a constant reason, with at most sixteen entries and a finite byte cap. Diagnose syntax/type/inapplicable-filter problems before admission; do not consume request identity for a request rejected before admission. Live/foreign identity assessment occurs only after current authority and at safe points. Echo no credentials, raw addresses, protected Scene data, invented handles or arbitrary unbounded user strings. Do not incidentally reject previously accepted unknown-property behavior on the legacy interface without an explicit compatibility path.

15. **Actual client-path verification.** Requiredness and omission are verified at the native schema, actual model/client exposure and transported argument seams; a rendered function signature alone is not proof. Standard Pi optional-forcing was not demonstrated by validation and is not assumed as the cause. Correct an adapter only where loss of omission is observed; otherwise improve examples and consider additive narrower object/layer/node surfaces. Provider support for structured discriminated unions must be demonstrated before relying on it. Never use blanket empty-string removal or nullable/default placeholders to make a mixed-kind request appear valid.

16. **Translation interface and shared preparation.** Add vsr_translate_preview and vsr_translate_commit as an additive pair on existing scene edits. Preview takes one Scene, a bounded selection of node handles with node-edit, structural and ancestry expectations, and a world-space delta. It uses the same live validation, transform conversion and prepared-edit representation as commit, not a separate simulator. Preview requires current Scene read/edit authority and node-edit exposure; read withdrawal protects preview contents just as it protects images. Host-local submissions follow equivalent rules under host authority.

17. **Selection normalization and eligibility.** Treat selection as a deterministic set. Duplicate entries with identical expectations collapse and are disclosed; conflicting expectations for the same handle reject. Keep only topmost selected roots, never an unselected upward/common parent. Valid selected descendants are reported as inherited rather than receiving another position write. All requested identities and normalization dependencies remain part of validation; dropping a descendant cannot hide a stale/foreign/unreadable selection entry. Only eligible scalar-transform roots move; no wrapper insertion, topology creation or automatic promotion of an object node. A selected root's unselected descendants move by normal inheritance, which is explicitly disclosed.

18. **World-space translation semantics.** Move the existing local origin of each root by the requested delta in world space, through the existing parent-transform conversion, while preserving its local linear transform. This is not geometry-centre or bounds-centre movement and does not add rotation, scaling or snapping. Parent rotation, shear and reflection are handled by the established affine conversion. Local zero/signed scales retain supported position-edit behavior when the required parent is invertible. Singular required parents reject without repair/pseudoinverse. Validate finite input/computation and float32 range using existing rounding rules; do not demand an exact double-to-float32 round trip for ordinary fractional values. Any invalid root rejects the whole plan.

19. **Preview content and nonperturbation.** Return normalized roots, inherited selected nodes, current/predicted origins, prepared local matrices, consumed dependency expectations, bounded effect warnings and an opaque preview reference. No Scene/renderer/view write, version increment caused by a write, device work, commit, Batch Reference or restore eligibility is introduced by preview. Observation of existing versions does not invent new mutation history. Bound ancestry and effect work; do not walk the whole Scene. Complete affected-descendant lists/counts require actual bounded enumeration; otherwise report unenumerated/truncated status and the fact of inherited effects, not a falsely complete impact analysis.

20. **Prepared-plan ownership and limits.** Own previews in the edit/Translation module, not SessionAuthority. Bind opaque references to existing launch/session/Scene identity, original normalized inputs and exact prepared dependencies. Selection cannot exceed the existing 64-entry edit ceiling; host policy may narrow it. Hosts must configure, validate and advertise finite per-session/global preview counts, aggregate bytes, operation/ancestry/effect limits and a positive monotonic lifetime before exposure; no unbounded or infinite default is permitted. Expiry, eviction, withdrawal and teardown reclaim prepared state without reviving references or dangling Scene/node ownership. Do not introduce a global map migration or shared-result-store redesign to implement this bounded capability.

21. **Exact preview commit and atomicity.** Commit takes the opaque prepared reference and its own request identity, not a new caller-modified translation. Recheck all relevant node, structural, ancestry, normalization, current authority/exposure and plan-lifetime conditions immediately before mutation. If anything correctness-bearing changed, return stale/conflict/expired failure and write nothing; never recompute a different plan or silently rebase. Use existing narrow dependency semantics so unrelated changes conflict only when they actually invalidate the prepared contract. Root eligibility and every planned allocation/restoration receipt must be settled before an all-or-nothing commit.

22. **Replay, consumption, cancellation and restore.** A successful commit consumes the prepared preview once. A different request cannot apply it again. Same admitted request replay returns the historical result under current visibility rules, even though the token was consumed; it does not mutate again. A definite stale plan cannot later revive through value change-back. Existing cancellation boundaries remain: before commit no writes; after commit truthful committed state and bounded cleanup, not implicit rollback. Return normal edit receipts, Scene revisions, precise resulting versions, Batch Reference and activity. Conditional one-way restoration must recover the exact original matrices when eligible, with existing target/ancestor history conflicts and permissions unchanged.

23. **Base, layering and scope alignment.** Before source implementation, select the intended integration lineage and prove required identity/edit/encoder/dependency work is present. A WIP label, bookmark or running executable alone is not proof of accepted source inclusion. Preserve local working-copy/configuration changes and unrelated workspaces. Reuse existing qualified modules and contracts instead of recreating completed prerequisites. This capability is generic VSR integration, with a viewer presentation adapter; it does not change Studio Project/Shot authority or protocols, image-import orientation, archive schemas or generic Data Tree behavior.

24. **Qualification before exposure.** Expose each metadata/frame/preview/commit capability only after its own complete external behavior and bounds are verified. Metadata does not enable capture; a CPU-only or controllable adapter test does not qualify current barney/CUDA presentation. Samples, render calls, polls, present submissions and readback completion are distinct observations; this feature does not promise quality units from those counts. Preserve unqualified/unavailable reasons and explicit partial rollout rather than declaring all four capabilities complete from one mock path.

## Testing Decisions

1. **Primary public seam.** Reuse the already confirmed public headless integration and admitted MCP interface with real Scenes and Image Pipeline sources. Test through the same metadata/frame/translation interface clients use, not private epoch/cache helpers or an alternate simulator. Source binding/presentation adapters are injected at the genuine host seam; headless real-pipeline and production viewer adapters provide actual variation.

2. **Real viewport evidence is required.** Validate presented-frame association through the actual viewer display-submission interface and then compare returned images with the chosen interactive viewport. Current barney/CUDA paths must be exercised where advertised. Mock-only snapshots, CPU-only success, operating-system screenshots used as diagnostic evidence and compilation are not sufficient to implement or qualify MCP viewport-frame parity.

3. **Provenance matrix.** Test independent agent versus interactive and generic hosts, actual bound versus merely selected/unrelated cameras, Device Identifier/subtype, renderer and pipeline settings, native/display size and scale, unknown fields, configuration changes, switching/teardown, closure/recreation and retired identities. Metadata queries must create no renderer, GPU work or persistent-state change. Existing legacy/headless renders retain behavior while their origin becomes explicit.

4. **Presentation identity and timing.** Use independent producer/presentation counters and delayed acquisition to distinguish produced, uploaded, submitted and readback-completed frames. A never-presented buffer cannot be returned as presented. Pin latest at the request safe point; delayed old readbacks cannot substitute silently. Displayed-but-older Scene state returns its actual pixels and matched rendered/current revisions. Do not stamp producer content with current settings after the fact.

5. **Image semantics.** Check asymmetric orientation patterns, known colors/alpha, output conversion, pipeline Outlines/effects, native size versus display scale, and exclusion of UI chrome/HUD/gizmos. Independently decode PNG pixels; do not use the same encoder bookkeeping as the correctness oracle. Preserve matching color/orientation metadata without double conversion. Compare real current viewport output rather than requiring the historical validation dimensions permanently.

6. **Frame lifecycle and authority.** Exercise cold cache, inactive/no presentation, qualified/unqualified sources, multiple-view selection, resize, device change, closure/recreation, historical replay, source expiry, read withdrawal before work/publication/retrieval, pending cancellation and safe drain. After withdrawal, minimal owner receipts contain no protected pixels or provenance. No independent-device fallback or extra render appears in any missing-source path.

7. **Resource and responsiveness proof.** Test checked size products/sums, image/frame/ref retention, slow consumers, exhausted staging/cache/result budgets, cancellation/teardown with in-flight work, and PNG/base64/response envelopes. Qualify acquisition setup, submission, polling, readback, encoding and release under concurrent normal viewing with actual latency/heartbeat/memory evidence. Keep existing capture qualification thresholds where applicable and state any observation-specific limit explicitly; do not treat a NO_WAIT label as proof of bounded service.

8. **Native request and model/client behavior.** Test minimal omitted object/layer/node filters, exact empty-name filtering, intersections, mixed-kind and malformed/invented-handle rejection, bounded diagnostics and pre-admission request-ID behavior. Check actual client/model-facing schema and transported arguments, not only server declarations. Native omission-based protocol fixtures provide prior art; close the legal live Queen_B/pawn query gap with fresh IDs and a legitimate omission-preserving route that does not steal the existing controller.

9. **Knowledge delivery.** Tool descriptions and structured results must present actionable inheritance/world-origin examples where the model sees them. Test resource/prompt guides only if the actual client loads them. Ensure meaningful omitted/empty distinctions survive examples and diagnostics. Do not relax validation to make a failing example pass.

10. **Preview normalization and math.** Use actual public Scenes and literal independently expected placements for ancestor-and-descendant selection, siblings, independent roots, duplicates, foreign/stale identities, ineligible roots, rotated/sheared/reflected parents, local signed/zero scales, singular required parents, fractional deltas and overflow. Both selected origins move once; child local transforms remain unchanged when movement is inherited. Upward promotion/common-parent guessing must fail negative controls. Preview itself changes no Scene or restore state.

11. **Plan dependency and storage matrix.** Change relevant target state, parent transform/topology, selection-normalization relationships, authority/exposure and source lifetime between preview and commit; all relevant changes fail atomically without silent rebase. Preserve allowed unrelated-change behavior. Test per-session/global count/byte caps, bounded ancestry/effects, expiry, eviction, foreign-session/launch references and current visibility of preview contents.

12. **Commit/replay/cancel/restore.** Test preview to commit to reinspection to exact conditional restore through the public interface and thin MCP adapter. Same-request replay never applies twice; different requests cannot reuse a consumed preview. Failure leaves every selected root unchanged and creates no false restore eligibility. Cancellation before/after commit reports actual state. Later target/ancestor writes, including change-back, preserve existing restore conflicts.

13. **Existing regression prior art.** Keep native node-filter and wire-token edit/restore fixtures, local/world/mixed edit tests, ancestry/lifetime precision, permissions/replay, image normalization/PNG bound, Frame/RenderIndex/Image Pipeline behavior and generic Forest/Data Tree regressions passing. Previously observed 97/133 assertion totals are baseline observations, not immutable counts; new tests may add assertions while preserving old behavior.

14. **Build and memory evidence.** Reuse the chosen existing CUDA/UI/network/Lua/SDL3-enabled build configuration, build and install changed libraries before runtime tests, and run applicable full CTest and integration suites. Exercise available sanitizers/Valgrind on snapshots, source teardown, prepared plans, copy/move/reuse and cancellation. Record actual matched counts, tool exits and limitations; possible driver allocations and unsupported instrumentation remain disclosed. Do not claim GPU memory, GUI interoperability or full-suite success from a narrow passing test.

15. **Live acceptance and closure.** With explicit permission for any live editing/restart, verify actual device/camera/renderer changes, resize/scale and pipeline effects on the chosen viewport, fresh inspection, once-only selection translation and restore. Geometry coordinates/ancestry come from fresh runtime observations, not file naming or historical handles. Preserve user configuration and Scene state. Each requirement maps to a new public regression or named unchanged test; independent correctness/Standards review and honest coverage limits accompany completion. No cumulative acceptance is inferred from a single partial slice or bookkeeping change.

## Out of Scope

- Fresh target-camera rendering, progressive-quality/sample/denoise certification and the existing one-off capture implementation; those retain their separate contracts.
- UI/desktop screenshots, ImGui chrome/HUD/gizmo composition, physical monitor scanout or color-management parity beyond documented viewport image output.
- Hidden rescaling or increases to current image/transport limits; implementing 8K-plus rendering now. Larger future output remains a requirement for later explicit policy, memory and transport evolution.
- Arbitrary world rotation/scaling, topology creation, wrapper nodes, common-parent/piece guessing, bounds-centre movement, chess rules or move legality.
- Geometric bounds computation/providers/cache publication and completion of unrelated scene-bounds tickets by implication.
- New authentication/private descriptor fields, Studio Project/Shot protocol changes, archive schema changes, image-import orientation changes or generic Data Tree redesign.
- A global map migration, shared-result-store refactor, broad notification overhaul or implementation of unrelated triage suggestions.
- Source implementation, ticket publication, running-viewer restart, live Scene edits, workspace reset/rebase or user-configuration changes merely because this spec was written.

## Further Notes

### Synthesis defaults and readiness

This document synthesizes the four requested directions and explicitly adopts the draft plan's recommended defaults. No separate recorded answers to the four proposed defaults are claimed. The document is behaviorally specified and labeled ready-for-agent for the local tracker; publication is not a claim of prior runtime qualification, silent resolution of every historical issue, or permission to implement/restart/edit without the next authorized workflow.

The primary headless/MCP test seam with real Scene/Image Pipeline sources is reused from the earlier confirmed approach. Presented-frame fidelity additionally requires a genuine viewer producer/presentation seam and real interactive evidence; this requirement is explicit rather than hidden behind a new private mock interface. There is no additional interview in this synthesis.

### Related artifacts and immutable contracts

The [draft plan](plan.md), [live validation report](../viewport-validation/report.md) and [viewport handoff](../viewport-frame-handoff.md) contain evidence, recommended sequencing and limitations. They are not implementation results for this spec. The operating-system viewer-window capture is diagnostic evidence only.

Reuse and preserve the [camera/capture spec](../camera-capture/spec.md), [broad-editing spec](../broad-editing/spec.md) and [scene-dependency/ownership spec](../scene-bounds-invalidation/spec.md). Overlapping view observation/policy work should map to existing camera-capture contracts rather than duplicate completed prerequisites. Presented Frame Observation and Selection Translation are additive behaviors. Existing frozen specs/addenda are not rewritten; any later authorized extension uses a new additive artifact with explicit compatibility. Historical parent tickets remain untouched by publication.

Relevant domain language is Scene, Layer, LayerNodeData, Forest, Image Pipeline, Image Pass, Outline, Device Identifier, ANARI Library, Device Subtype, SceneRevision, InspectionRevision, ViewRevision, Operation Reference and Batch Reference. Respect [image-orientation ADR 0014](../../docs/adr/0014-store-images-in-anari-orientation.md) without conflating imported sampler images with viewport output normalization, and [authority ADR 0030](../../docs/adr/0030-split-authority-between-round-tripped-structure-and-optimistic-parameters.md) without changing Studio's separate client-server contract.

### Base and execution caution

Live validation occurred in a working copy with local .envrc and .pi/mcp.json changes and an older integration snapshot. Running/installed/build-tree viewer binaries matched, but that does not establish inclusion of all later accepted source work. Before implementation, select the intended verified lineage, preserve local state and establish a green baseline; do not start from bare trunk if it drops required integration/edit/encoder functionality. Existing longer executor authorization, qualification records and remaining cumulative acceptance state must be read, not inferred from a WIP title or bookmark.

This spec publication creates no ticket graph or implementation dispatch and changes no capture permission, image cap, persistent viewport/camera state, credentials or user artifact. Local-first only; no remote issues, PRs or pushes are authorized.
