# Embedded MCP adapter: carrier, placement and code mapping

This is the binding carrier-mapping deliverable of spec §9. The adapter maps
the headless integration API onto MCP. It adds no semantics of its own. No
client needs to parse English: every decision below is carried by a
machine-readable field, and `message` strings are explanatory only.

- Transport: `McpServer.hpp`. It uses HTTP/1.1 on loopback, MCP streamable
  HTTP with protocol `2025-06-18`, and JSON-RPC 2.0, one request per POST.
- Tools: `tools/`. Each family adds its `mcp::Tool` entries to
  `mcp::Config::tools`, and all of them share `tools/ToolSupport.hpp`:

  | Header | Tools | Ticket |
  |---|---|---|
  | `tools/QueryTools.hpp` | `vsr_discover`, `vsr_inspect` | 103 |
  | `tools/LookupTools.hpp` | `vsr_operation_status`, `vsr_cancel` | 103 |
  | `tools/EditTools.hpp` | `vsr_edit`, `vsr_restore` | 104 |
  | `tools/RenderTools.hpp` | `vsr_render`, `vsr_render_image`, `vsr_resynchronize` | 105 |

## Payload placement

A call either succeeds or is rejected.

**Rejected** means nothing was admitted, or the lookup or cancel failed. The
response is a JSON-RPC `error`:

```json
{"error": {"code": -32000, "message": "<explanatory>", "data": {"code": "<frozen code>"}}}
```

`error.data.code` is the contract. `error.code` is `-32000`, the
implementation-defined server-error range, for every semantic outcome. The
exceptions are the carrier errors `-32600`, `-32602`, `-32601` and `-32700`
listed below.

**Admitted, or record found** returns a JSON-RPC `result` in MCP tool-result
form:

- `structuredContent` holds the status object described below. It is the
  contract.
- `content` holds one `{"type":"text","text":…}` item containing the same
  JSON, for clients that only read text.
- `isError` is `true` exactly when the reported operation is terminal with an
  outcome other than `ok`. The frozen code is then in
  `structuredContent.outcome`.

### Status object (`structuredContent`)

| Field | Presence | Meaning |
|---|---|---|
| `operation_ref` | always | `{session_id, request_id}`. It is the complete qualified reference, including when the lookup names an earlier session's record |
| `kind` | always | Retained operation kind: `discover`, `inspect`, `edit`, `restore`, `render`, `resynchronize`. Preserved for lookups from later sessions |
| `state` | always | `pending` or `terminal` |
| `progress` | while a slot is held | A frozen non-terminal name (see below). `awaiting_cleanup` can coexist with `state: terminal` |
| `outcome` | once terminal | A frozen code (see below) |
| `committed` | always | `true` iff the scene mutation committed. It is independent of `outcome` |
| `result_visible` | always | `false` for the minimal receipt, when current read is missing on a covered target |
| `result` | when `result_visible` | The operation's JSON result, e.g. a discovery or inspection page (`revision`, `visited`, `returned`, `items`, `complete`, `stop`, `cursor`; inspection also `scene_revision`) |
| `replayed` | admissions only | `true` when an identical retry returned the retained record |

A submission returns this object immediately as the admission
acknowledgement. Results arrive through `vsr_operation_status`. Tools never
block on a safe point.

## Tools (ticket 103)

| Tool | Arguments | Capability | Slot / ID |
|---|---|---|---|
| `vsr_discover` | `request_id`, `page_size?`, `cursor?` | read | one scene slot, one request ID |
| `vsr_inspect` | `request_id`, `scene_id`, `kind?`, `name?`, `name_contains?`, `handle?`, `object_type?`, `object_subtype?`, `node_kind?`, `layer_handle?`, `parent_handle?`, `page_size?`, `cursor?` | read on `scene_id` | one scene slot, one request ID |
| `vsr_operation_status` | `operation_ref` | current proof; read on every covered target for payload or foreign records | control gate, no ID |
| `vsr_cancel` | `operation_ref` (own session only) | current proof | control gate, no ID |

Inspection defaults to `kind: "objects"`; `kind: "layers"` and `kind: "nodes"`
are also supported. Object filters intersect. `name` and
`object_subtype` compare exactly, `name_contains` is case-sensitive literal text
(not regex/glob), `object_type` uses the advertised ANARI enum strings, and
`handle` is the opaque object handle returned by inspection. Filter strings are
bounded to 4096 UTF-8 bytes by default. Malformed or unsupported filters reject
before admission; a stale exact handle returns `conflict`, and no matches return
an empty complete page (or a marked work-budget continuation).

Layer queries support intersecting exact `name`, literal case-sensitive
`name_contains`, and exact layer `handle` filters (or unfiltered pages).
Matching uses the real stored layer name, not its display preview or Scene
LayerMap key; duplicate names are legal and no matches succeed empty.
Object/node-only filters reject for layers. Each item has
`handle`, `name` (the bounded display of the stored Layer name, not its map key),
`active`, and `root_handle`. Inactive layers remain readable. Object, layer and
node handles have distinct host-issued domains and are not interchangeable.
Removed/recreated layers, scene reset and SceneIdentity replacement retire old
handles permanently. Clearing a layer's descendants preserves its genuine root
and its handle, matching Forest's root-preserving semantics.

Node queries support intersecting `name`, `name_contains`, node `handle`,
`node_kind` (`empty`, `transform`, `object`, `transform_array`), `layer_handle`,
and immediate `parent_handle` filters. They traverse layers in scene order and
nodes in stable pre-order, including each layer root. Records contain
`handle`, `layer_handle`, `parent_handle` (null for roots), `root`, bounded
`name`, `node_kind`, `enabled`, and `effective_enabled` (layer and ancestry).
Hidden placements remain readable. Scalar transforms expose a complete
`local_matrix`; references expose `object` or `array` with type and handle or
an explicit unavailable state. Arrays include metadata summaries only, never
mapped contents or an arbitrary instance matrix. Parent handles resolve even
outside a filtered page. Ancestry/climb work uses the shared visit/slice budget
and cursors retain its bounded continuation state. Cursors bind all filters,
kind, page policy and inspection revision; identical retries replay history.
Forest creation stamps establish genuine node lifetime: erasure/recreation,
ancestor removal and scene replacement cannot redirect handles. Subtree copies
have distinct handles; moving a live subtree preserves them. Every node record
also exposes `node_edit_version`, an opaque string to copy verbatim, bound to
that live node and the host launch/scene registration. It covers local transform
and own-enabled writes, conservatively within the node. Even same-value writes
and change-away/back advance it, independently of delegate publication. Rename,
parent/sibling writes, other layers and layer activation do not advance the
node's local token. Records also expose opaque `structural_version` and
`ancestry_version` strings, to copy verbatim. Both bind the actual live node,
layer membership and launch/registration. The structural token covers only
creation/reparent history on the target and its parent chain; sibling insertion,
erasure, reordering and state do not invalidate it. The ancestry token adds
ancestor payload/transform write history (not the target's local content).
Parent/grandparent transform change-back invalidates ancestry but not structure;
ancestor enabled-only writes invalidate neither.

Scalar transforms expose complete `local_matrix` and `local_position`;
all node kinds expose `world_matrix` when their ancestry is valid. Matrices
are four rows of four numbers with column-vector mathematics and translation
in the last column. World composition is parent times local; non-transform
nodes inherit their parents without acquiring transform storage. Non-finite
local components retain typed `non_finite` tags (never substituted zeros).
World observations instead carry `state: unavailable` and a reason for
non-finite/non-affine transforms, array ancestry, or composition overflow;
a singular finite affine matrix still has a valid world observation.

`rotation` contains VSR degree-based azimuth (Y), elevation (X), roll (Z),
composed Y then X then Z. `scale` and rotation are available only when the
finite cached SRT reconstructs every linear component within
`1e-5 * max(1, abs(component))`. Translation is observed directly, not repaired.
Shear and unfaithful reflection/degeneracy expose `unfaithful_srt_cache`;
faithfully cached zero/negative scale remains available. Euler values are not
unique and no world SRT approximation is provided. `edit_affordances` reports
representability with explicit availability/reasons, not permission or a
promise that a node-write API exists. Scalar full-matrix replacement remains
representable even when its current matrix is invalid. Empty/reference/array
nodes do not advertise scalar matrix/position/rotation/scale conveniences.
World accumulation uses constant-size cursor/scratch state and the same
one-ancestor-per-visit budget as dependency observations.

These are ordered event observations, not final-parent/value hashes or whole-layer
epochs: each Forest creation/reparent event stamps only the affected node with
that Forest's next event number. Taking the latest event on the current chain
catches every reparent, including change-back: a new move event is later than
all previously observed chain events. Transform/payload writes likewise use an
ordered clock and update only the written payload. Construction/relocation keeps
history; clone lifetimes/handles remain distinct. An ancestry token includes
structure, so removing a formerly latest ancestor cannot resurrect a token.
The existing one-ancestor-per-visit walk accumulates two integers; authenticated
cursors retain them across work-limited pages. No recursive observation,
per-ancestor retained identity, full snapshot or uncharged scratch is introduced.
`SceneIdentity::beginNodeDependencies` / `advanceNodeDependencies` expose the
same incremental check to headless callers. Each advance must be charged to the
caller's work budget; only complete walks may be compared with `matchesStructure`
or `matchesAncestry`. Callers must operate at safe points and abort/restart an
in-progress walk if inspection revision changes between slices. An edit checker
must resolve the target and build a fresh bounded walk, never compare two stored
old walks as evidence of current state.

Hosts must mutate and query on the safe-point thread. Normal Scene notifications
advance revisions incrementally; no query-time hierarchy snapshot or fingerprint
scan is used. After changing `Layer::editableName()` or using raw Forest/node
mutations, call `Scene::signalLayerStructureChanged(layer)` (or
`signalLayerTransformChanged` for transforms). Scene layer membership/activation
and `Layer::clear()` notify automatically. Unsupported raw mutation without the
appropriate notification cannot maintain cursor/render correctness. These
notifications do not regenerate unchanged lifetime handles.

Use ordinary `LayerNodeData` setters (matrix/SRT, enabled, empty/object/array,
`setValueRaw`), reset, or payload assignment for editable content. These maintain
per-node write history without requiring an integration-specific setter. Copy
construction preserves content/history but Forest assigns a distinct lifetime;
storage relocation preserves live identity and history. Assignment advances the
destination's history rather than importing the source's version. Moving out of
live payload also advances the source's history. Transform setters preserve the
stored default unless an explicit default is supplied; resetting uses that
baseline, while changing baseline metadata alone is not a current-content edit.
Hosts must still publish the appropriate Scene layer signal after writes (one
coalesced signal is sufficient), including renames, to keep cursors, render
indices and UI current. Bypassing setters through raw-memory writes is unsupported
and cannot maintain edit history, even if a later layer notification is sent.

Structural mutation audit: Forest child insertion (including explicit-slot
insertion) issues a fresh lifetime and creation event; `copy_subtree` uses that
path for each clone; `move_subtree` stamps only the moved subtree root, preserving
its identity and all descendants. Every descendant walk includes that root.
Erase/subtree erase/clear retire descendants through real pool lifetime checks;
Scene layer removal/replacement retires the layer domain. Root-preserving clear
need not invalidate the surviving root's own parent-chain dependency. Raw Forest
operations require the structure signal after mutation; Layer clear and Scene
layer operations already publish. Existing Viewer/Lua/importer hierarchy paths
use these same insert/copy/erase primitives. Cross-Forest relinking or raw pointer
rewriting is not a supported layer-membership operation; copying into another
layer creates fresh nodes owned by that layer.

Representative mutation-path audit (source inspection, not interactive testing):
- Viewer: `BaseViewport.cpp` gizmo and `ObjectEditor.cpp` SRT/reset use the
  tracked setters and transform signal; `LayerTree.cpp` uses `setEnabled`.
- Lua: `LayerBindings.cpp` uses enabled/matrix/SRT/array setters. These payload
  bindings do not publish automatically; scripts must use Scene's exposed
  `signalLayerStructureChanged` / `signalLayerTransformChanged` bindings.
- Animation: `TransformBinding.cpp` uses the matrix setter and transform signal;
  `UsdInstancerFileBinding.cpp` replaces arrays through the tracked setter and
  structure signal. Array-content edits are not scalar placement transforms.
- Import: GLTF uses `Scene::insertChildTransformNode` (tracked construction and
  automatic structure publication); layer deserialization uses SRT/raw-value
  and enabled setters. Hosts must publish after deserializing into a live layer.
Headless tests exercise these shared core mutators, assignment, default baselines,
copy/move/relocation and coalesced publication; they do not claim execution of
optional UI/Lua/USD front ends in the CPU-only build.

Discovery scene items advertise `inspection.kinds`, `default_page_size`, and
`max_page_size`. Both query schemas/descriptions use the host's active limits:
default 64 and maximum 256 unless configured otherwise. Oversized pages reject
before admission. Cursors bind the entire filter set and effective page size;
changing either returns `cursor_stale`. Identical request replay retains its
historical page rather than observing fresh state.

- Revision tokens (addendum 04 §8): an inspection page's `result` carries
  `scene_revision`, and each discovery `view` item carries `view_revision`,
  both in the `vsr_render` token form. Discover → inspect → render therefore
  needs no host-side token lookup. The discovery page `revision` and cursor
  do not cover `view_revision`; re-discover for a fresh view token.

## Tools (ticket 104)

| Tool | Arguments | Capability | Slot / ID |
|---|---|---|---|
| `vsr_edit` | `request_id`, `scene_id`, `edits: [{handle, version, parameter, value}]` (1–64) | edit on `scene_id` | one scene slot, one request ID |
| `vsr_restore` | `request_id`, `scene_id`, `target_batch_ref: {launch_id, batch_id}` | edit on `scene_id` | one scene slot, one request ID |

- `handle` and `version` are copied verbatim from a `vsr_inspect` item
  (`handle` is 32 lowercase hex, domain then serial; the adapter qualifies it
  with the current launch). `value` is the §8 edit-surface JSON value; the
  headless value rules accept or reject it at the safe point.
- Node writes require explicit host registration with `SceneRegistration::nodeEdits`
  (or `ContextSceneOptions::nodeEdits`) in addition to the session's scene
  edit grant. Discovery scene items advertise `node_edits_exposed`; existing
  object grants alone do not expose nodes. The integration-enabled viewer opts
  in. Supported fields are own `enabled` on any existing node kind and local
  `position` or full local `transform` replacement on scalar-transform nodes.
  None changes layer activation or stored default baselines. World positioning,
  rotation, and scale conveniences are not exposed.
- A node entry is `{target_kind: "node", handle, version, structural_version,
  parameter: "enabled", value: <Boolean>}`. Copy `handle`, `node_edit_version`
  (as `version`), and `structural_version` verbatim from node inspection.
  `structural_version` is required even for enabled-only edits: it pins the
  inspected target/layer/parent-chain history, not ancestor transform state.
  Missing/malformed tokens, non-Boolean values, or unsupported fields reject
  before admission. Live target writes (including change-back) or relevant
  topology changes conflict; unrelated sibling and live-parent state writes do
  not. Local position uses the same narrow structural dependency: parent
  transform changes alone do not conflict. All entries share the existing
  64-entry batch and authority limits.
- Local movement uses `{target_kind: "node", handle, version,
  structural_version, parameter: "position", value: [x, y, z]}`. Optional
  `space: "local"` is equivalent to omission. Exactly three finite binary64
  numbers within float32 range are required; values are correctly rounded,
  never clamped. Empty/object/transform-array nodes reject without mutation.
  Only the translation column changes: shear, reflection, singular linear
  parts, cached SRT scale/angles, and reset defaults are preserved. Position
  and enabled entries may share a node with one expected version, in either
  order; repeated fields or inconsistent versions reject. Multi-node groups
  prepare and commit atomically and publish one final version per node.
  Preparation checks at most 4096 ancestor steps per batch; excess returns
  `busy` without mutation or lost restore eligibility.
- A `transform` value is four rows of four finite binary64 numbers, using
  column-vector mathematics and last-column translation. The last row must be
  exactly `[0,0,0,1]` before binary32 rounding. Wrong shape/type, non-finite or
  overflowing input, and projective rows reject unchanged with a structured
  `value_error` and `failed_entry`; non-scalar nodes return
  `unsupported_node_kind`. Singular, sheared and reflected affine matrices are
  accepted without reconstruction or lossy repair. `transform` may combine
  with `enabled` but not position/rotation/scale or another matrix entry.
  Restore preserves the exact prior matrix and cached SRT representation,
  including noncanonical angles and unfaithful caches, without replacing the
  default baseline. Position/rotation/scale are not exposed by this slice.
- `vsr_restore` has its own `operation_ref`, distinct from the target batch
  and from the original edit's provenance. It never selects a batch
  implicitly. Changing `target_batch_ref` under the same `request_id` is
  `request_id_conflict`.
- On commit, `structuredContent.result` is `{"batch_ref": {launch_id,
  batch_id}, "committed": true, "scene_revision": "<token>", "versions":
  [{handle, version}]}` for an edit and `{"target_batch_ref": {…},
  "committed": true, "scene_revision": …, "versions": […]}` for a restore
  (addendum 04 §6, §8). `versions` lists each affected object's new
  parameter version, in `vsr_inspect`'s `handle` form, ready for the next
  edit; `scene_revision` is the resulting scene token for `vsr_render`. All
  are present for `applied_with_sync_failure` too, because that outcome
  committed. Node edit/restore results additionally carry `node_versions:
  [{handle, version, structural_version, ancestry_version}]`, with fresh opaque
  tokens in inspection's format. Node-only results keep `versions: []`; no
  object parameter token is invented. Object-only result shape is unchanged.
  Restore changes only recorded fields (own enabled flag and/or exact local
  matrix with its cached representation), validates target and structural
  history, and preserves unrelated names, state, and reset defaults. It uses the
  same journal/activity budgets, permanent conflict, consumption, expiry,
  replay, local/external authority, and synchronization behavior as object
  Restore. All batch state is committed before coalesced layer notifications
  and one normal scene-sync acknowledgement; sync failure is not rollback.
- A value rejection's `result` is `{"committed": false, "failed_entry": <0-based
  index>, "value_error": "unsupported_parameter"|"incompatible_type"|
  "non_finite"|"out_of_domain"}`.
- A restore of an expired batch reports `outcome_unavailable` with `result`
  `{"reason": "expired"}` (addendum 04 §7). Unknown, wrong-launch and
  foreign-scene targets carry no `result`.

| Outcome (`kind`) | `committed` | When (headless source) |
|---|---|---|
| `ok` (edit/restore) | true | Commit and synchronization confirmed |
| `applied_with_sync_failure` (edit/restore) | true | Commit, then the normal-sync hook failed; the batch stays restorable (edit) or is consumed (restore) |
| `conflict` (edit) | false | A target was removed, replaced or changed since its `version` (ticket 62 §2) |
| `conflict` (restore) | false | An affected object changed since the batch or a parameter is gone; nothing mutates, the batch becomes conflicting (payload released) and retries report `conflict` again (ticket 121) |
| `already_restored` (restore) | false | The target batch was consumed by an earlier restoration |
| `outcome_unavailable` (restore) | false | Unknown, expired, wrong-launch or foreign-scene `target_batch_ref` (spec §11; ticket 73 §2–3). Expired carries `result.reason: "expired"` (addendum 04 §7) |
| `invalid_request` (edit) | false | A value failed the edit surface at the safe point; the ID is consumed (ticket 62 §2). `result` carries `failed_entry` and `value_error` (addendum 04 §6) |
| `busy` (edit/restore) | false | Preparation or restoration-store reservation failed (tickets 62 §1, 73 §1) |
| `cancelled`, `timeout`, `forbidden` | false before commit | As for every scene operation |

Envelope errors return `error.data.code: invalid_request` and consume no ID:
missing or non-integer `scene_id`, an empty or over-64 `edits` list, a
duplicate (object, parameter), a malformed `handle` or `version`, a non-string
`parameter`, a missing `value`, or an incomplete `target_batch_ref`. A scene
without the edit grant rejects with `forbidden` (HTTP 403).

`request_id` must be a JSON number that is integer-valued and in
`[1, 9007199254740991]`. The `session_id` is not an argument: it is taken from
the authenticated `VSR-Session-ID`.

## Tools (ticket 105)

| Tool | Arguments | Capability | Slot / ID |
|---|---|---|---|
| `vsr_render` | `request_id`, `view_id`, `scene_revision`, `view_revision` | render + read on `view_id` | one scene slot, one request ID |
| `vsr_render_image` | `operation_ref` | as `vsr_operation_status` | control gate, no ID |
| `vsr_resynchronize` | `request_id`, `scene_id` | read + resynchronize on `scene_id` | one scene slot, one request ID |

- Revision tokens are strings `"<activation>.<nonce>.<domain>.<value>"`
  (decimal unsigned 64-bit fields), exactly as a successful render result
  echoes them. `vsr_inspect` (`scene_revision`), `vsr_discover` view items
  (`view_revision`) and committed edits/restores (`scene_revision`) return
  them too (addendum 04 §8).
- **Dimensions are view-owned.** A `vsr_render` call that names `width` or
  `height` is an envelope error (`invalid_request`, no ID consumed); the
  request is never resized. Invalid or oversized view dimensions (outside
  1–1024 per axis) also reject pre-admission with `invalid_request`
  (ticket 91). The view is never clamped.
- **The image is MCP image content**, never a file path. When a render is
  terminal `ok`, the caller has read on the view and its scene, and the
  image is still in the retained-image budget, `vsr_render_image` (and
  identical `vsr_render` replays) append
  `{"type":"image","mimeType":"image/png","data":"<base64>"}` to `content`
  after the text item. The PNG is RGBA8 at exactly the view's dimensions.
  `structuredContent.result` is `{width, height, scene_revision,
  view_revision, capture}`. `vsr_operation_status` returns the same status
  object without the image.
- `vsr_resynchronize` reports its result as
  `{"ready": bool, "state": "ready"|"needs_sync"|"synchronizing"|"failed"}`.
  A recovery that ran is terminal `ok` even when it failed; failure is
  `ready:false, state:"failed"` (ticket 67). No edit is replayed.

| Outcome (`kind`) | Where | When (headless source) |
|---|---|---|
| `ok` (render) | T | Tokens matched and the scene was ready at start and completion; image available |
| `superseded` (render) | T | Scene or view token no longer current, at start or at completion (e.g. a resize) |
| `sync_required` (render) | T | Tokens match but the view's scene is not `ready`, or the view is unconfirmed; no image |
| `busy` (render) | E | The view's backend resources are still held by earlier work (ticket 91), or the pool is full |
| `busy` (render) | T | Another render held the view when this one reached its safe point |
| `invalid_request` (render) | E | Envelope, `width`/`height` present, or invalid view dimensions |
| `invalid_request` (render) | T | View dimensions became invalid after admission |
| `unknown` (render) | T | The backend reported a fault, or image normalization failed |
| `ok` (resynchronize) | T | Recovery ran; read `result.ready` and `result.state` |
| `cancelled`, `timeout` | T | 30 s render deadline from admission; late images never succeed |
| `forbidden` | E, T | Unexposed target or missing grant |

## Frozen outcome codes

The first column says where each code appears. **E** means it is returned as
`error.data.code` and nothing was admitted or found. **T** means it is
returned as `structuredContent.outcome` of a terminal operation, with
`isError` set.

| Code | Where | HTTP | Meaning / when |
|---|---|---|---|
| `ok` | T (`isError:false`) | 200 | Terminal success |
| `unauthenticated` | E | 401 | Missing or invalid launch credential |
| `invalid_session_proof` | E | 401 | Missing, wrong or stale session ID/proof, including an old session's proof |
| `session_expired` | E | 401 | The lease lapsed |
| `forbidden` | E, T | 403 / 200 | E: the capability is absent, the target is not exposed, a foreign record lacks read on a covered target, or the call cancels another session's work. T: permission was withdrawn before the safe point |
| `occupied` | E | 200 | Attach while a session is held (`vsr/attach` only) |
| `busy` | E, T | 200 | E: the scene pool or control gate is full, and nothing was admitted or consumed. T: a render view is held (105) |
| `request_id_conflict` | E | 200 | The same `request_id` was reused with different canonical arguments |
| `outcome_unavailable` | E | 200 | The ID is at or below high-water with no retained record, or it is an unknown foreign reference (ticket 79 §4). Restore also returns it for expired batches (104) |
| `unknown` | E | 200 | Own reference above high-water: it may be in transit, which confirms neither non-execution nor cancellation |
| `cursor_stale` | T | 200 | The page's revision changed: membership, metadata, values or discovery capabilities |
| `item_too_large` | T | 200 | One item exceeds the per-item byte cap |
| `cancelled` | T | 200 | Cancelled by `vsr_cancel` or because its session ended |
| `timeout` | T | 200 | The logical deadline passed (5 s for discovery and inspection) |
| `conflict` | T | 200 | Edit or restore precondition failed. **No scene mutation** (`committed:false`). `kind` (`edit` or `restore`) tells the two cases apart (104) |
| `superseded` | T | 200 | Not produced by the 103 tools; 104/105 document their use |
| `already_restored` | T | 200 | Restore target already restored (104) |
| `sync_required` | T | 200 | Render refused because the scene needs synchronization (105) |
| `applied_with_sync_failure` | T | 200 | **Committed** (`committed:true`) but synchronization failed (104/105) |

Commit versus no commit is always read from `committed`, never inferred from
`outcome`. For example, `applied_with_sync_failure` has `committed:true`,
while `conflict`, `cancelled` before commit, and `timeout` before commit have
`committed:false`.

## Progress states

`structuredContent.progress` uses exactly the frozen names: `queued`,
`running`, `yielded`, `synchronizing` and `awaiting_cleanup`. The last means
logically cancelled or timed-out work that still holds resources: `state` is
`terminal` and the slot is still held. Scene synchronization states (`ready`,
`needs_sync`, `synchronizing`, `failed`) appear only in
`vsr_resynchronize`'s `result.state`.

A committed edit or restore whose normal-synchronization hook is executing
reports `synchronizing` with `committed:true`, not `running` (addendum 03
§5).

## Carrier-level errors (not frozen codes)

`invalid_request`, `invalid_host` and `forbidden_origin` are documented
non-frozen codes (addendum 03 §1, §12). Clients must not treat any of them
as authentication failure. `invalid_request` also covers an empty,
oversized or duplicate-entry edit batch, invalid view dimensions and
invalid member values; whenever it is detected before admission it
consumes no ID.

| `error.data.code` | HTTP | JSON-RPC `error.code` | When |
|---|---|---|---|
| `invalid_request` | 200 | -32000 | Envelope error before admission: malformed `request_id`, `page_size` or `operation_ref`, a tampered or foreign cursor, or an unregistered target. No ID is consumed (tickets 37 §2, 55) |
| `invalid_request` | 200 | -32602 | `tools/call` without `name`, or an unknown tool |
| `invalid_request` | 200 | -32601 | Unknown method |
| `invalid_request` | 400 | -32700 / -32600 | JSON parse error or bad JSON-RPC envelope |
| `invalid_host` | 400 | -32600 | Host authority mismatch, or any forwarding header (ticket 97) |
| `forbidden_origin` | 403 | -32600 | An `Origin` header is present (ticket 97) |

Other addendum 03 policies the adapter surfaces unchanged:

- Status and result lookups never take the last control slot; identical
  in-flight renew, cancel or detach calls from one session share a slot;
  the excess returns `busy` (§2).
- Permission withdrawn before the safe point ends the work `forbidden`;
  work cancelled because its session ended ends `cancelled` (§3).
- A preparation or allocation failure ends terminal `busy` with
  `committed:false` (§4).
- `vsr_resynchronize` ends terminal `ok` with result
  `{"ready":true,"state":"ready"}` on success and
  `{"ready":false,"state":"failed"}` when the recovery ran but failed (§6).

HTTP 404, 405 and 415 without a body cover a wrong path, a method other than
POST, and a content type other than JSON.

## Open assumptions reflected here

The following tickets record current codes and policies. The adapter maps
them faithfully and does not decide them:

- 37: control priority, envelope code, and cross-session cancel returning
  `forbidden`.
- 55: inspection item shape, and cursor rejection as `invalid_request`.
- 79: foreign unknown returns `outcome_unavailable`, and the minimal own
  receipt.
- 91: render `busy` and invalid dimensions.
- 97: Host and Origin carrier codes.
- 115: discovery admission target and covered targets.
- 62, 73: edit and restore codes (value rejection, preparation `busy`,
  missing restore targets).
- 116, 117, 118: resolved by addendum 04 §6–8 (ticket 141).
