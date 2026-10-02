# Broad Editing: inspect and rearrange the live scene

**Status:** ready-for-agent
**Milestones:** 1
**Scope:** structured inspection and editing of existing layer nodes

## Problem Statement

An agent attached to a running VSR application cannot currently understand or
rearrange a typical imported scene using the VSR MCP tools alone. Object
inspection exposes most parameters only as names and types, and does not expose
layers, layer nodes, their hierarchy, or their transforms. The agent cannot tell
which placements reference a shared object, move an individual placement, or
hide it. It must page through unrelated objects because inspection lacks
filters and its schema does not advertise the maximum page size.

The first experiment used the glTF chess scene "A Beautiful Game". Describing
the scene and arranging Fool's Mate required reading source, rebuilding the
hierarchy in a separate Lua process, and manually executing a Lua script in the
viewer. Offline reconstruction is not the live scene and can render differently.
The existing material/light edit surface does not solve placement editing.

The immediate need is to read enough of the live scene to identify the four
pieces involved in Fool's Mate, move those existing placements in one safe
batch, verify the result, and restore their original state. The queen's-eye
camera, custom capture settings, and image-file export are a later increment.

## Solution

Extend the existing host-owned VSR integration so structured tools can inspect
objects and layer hierarchies and edit existing layer nodes. Reading is broader
than writing: the agent can read ordinary parameter values and object
references without gaining permission to edit every parameter it sees.

Expose opaque identities for layers and nodes, local and derived world
transforms, enabled state, object-reference links, and predictable filtering.
Permit position, rotation, scale, complete local-matrix, and enabled-state edits
on the appropriate existing nodes. Use VSR's existing degrees-based
azimuth/elevation/roll convention, preserve full matrices when moving a node,
and reject convenience edits that would silently discard shear or other
unrepresentable state.

Reuse Agent Edit Batches, Operation References, Batch References, host safe
execution points, authorization, synchronization, activity, and conditional
Batch Restoration. A batch commits completely or leaves scene state unchanged.
A stale queen edit must not fail merely because an unrelated pawn moved, but it
must fail when its target or relevant ancestor state changed. All work operates
on the running application's scene; no offline reload or arbitrary live script
execution is needed.

## User Stories

1. As an agent, I want to inspect the running application's scene, so that my decisions describe the state the user actually sees.
2. As an agent, I want parameter values as well as names and types, so that I can explain materials, lights, cameras, and renderers without reading source.
3. As an agent, I want read-only values for parameters outside the edit allowlist, so that inspection is useful without granting broader write authority.
4. As an agent, I want typed scalar, vector, matrix, Boolean, and string representations, so that I do not have to infer types from formatted text.
5. As an agent, I want object-reference values to link to opaque object identities, so that I can follow a surface's geometry and material relationships.
6. As an agent, I want missing or invalid references identified explicitly, so that I do not mistake them for valid objects.
7. As an agent, I want array shape and element-type summaries, so that I can understand geometry and texture inputs without downloading bulk data.
8. As an agent, I want unsupported or omitted values marked explicitly, so that partial inspection cannot be mistaken for complete state.
9. As an agent, I want to list layers and their active state, so that I can understand how the scene is composed.
10. As an agent, I want to inspect layer-node hierarchy and parent relationships, so that I can distinguish placements from shared objects.
11. As an agent, I want node names and kinds, so that I can identify a named chess piece and determine which edits it supports.
12. As an agent, I want to inspect disabled nodes and inactive layers, so that hidden state is not silently omitted.
13. As an agent, I want each object-reference node linked to its scene object, so that I can identify multiple placements of the same surface.
14. As an agent, I want local matrices and derived world matrices, so that I can understand both hierarchical and scene-space placement.
15. As an agent, I want effective enablement distinguished from a node's own enabled flag, so that I can understand why an enabled child may not be visible.
16. As an agent, I want transform-array nodes identified as such, so that I do not treat an array of instances as one ordinary transform.
17. As an agent, I want an opaque handle that addresses one node unambiguously, so that duplicate names cannot redirect an edit.
18. As a user, I want removed-node handles never to identify replacement nodes, so that stale work cannot move the wrong piece.
19. As a user, I want scene replacement to invalidate old layer and node identities, so that work from an earlier scene cannot affect the new scene.
20. As an agent, I want exact-name and name-substring filters, so that I can find a queen without paging through every object.
21. As an agent, I want object type/subtype and node-kind filters, so that I can inspect the relevant kinds of state.
22. As an agent, I want inspection scoped to a layer or immediate parent, so that I can navigate a hierarchy incrementally.
23. As an agent, I want direct handle lookup, so that I can refresh a known target without relying on its name.
24. As an agent, I want advertised page defaults and maxima, so that I can construct a valid request without a trial-and-error round trip.
25. As an agent, I want bounded paged queries with explicit completion and stop reasons, so that large scenes remain usable.
26. As an agent, I want cursors bound to the query and relevant scene revision, so that continued pages cannot silently combine incompatible observations.
27. As an agent, I want a no-match query to complete normally with no items, so that absence is not confused with a failed request.
28. As an agent, I want to set an existing transform node's local position, so that I can move one placement while preserving its other transform components.
29. As an agent, I want to set its world-space position, so that I can place it without manually inverting all parent transforms.
30. As a user, I want a position-only edit to preserve the matrix's linear part, so that moving an imported object does not alter its rotation, scale, reflection, or shear.
31. As an agent, I want degrees-based azimuth/elevation/roll edits matching VSR, so that rotations behave consistently with the node editor.
32. As an agent, I want local scale edits, so that I can resize a placement without replacing its geometry.
33. As an agent, I want an explicit full local-matrix edit, so that I can represent affine transforms beyond the convenience SRT model.
34. As a user, I want lossy rotation/scale convenience edits rejected, so that the agent cannot silently normalize an imported matrix.
35. As an agent, I want to toggle an existing node's enabled flag, so that I can hide or show a placement without deleting it.
36. As a user, I want transform edits rejected on non-transform nodes, so that object references are not overwritten or implicitly wrapped.
37. As a user, I want finite numeric and affine-matrix validation, so that malformed transforms do not corrupt the live scene.
38. As an agent, I want multiple edits on one node combined predictably, so that position, rotation, scale, and enablement do not depend on entry order.
39. As an agent, I want several node edits committed in one batch, so that the four chess moves appear together.
40. As an agent, I want existing supported object-parameter edits and node edits to share a batch, so that a compound adjustment can remain atomic without expanding the parameter allowlist.
41. As a user, I want invalid or stale entries to reject the entire batch before mutation, so that valid entries cannot be partially applied.
42. As a user, I want unchanged targets to remain editable after unrelated node changes, so that the whole layer is not unnecessarily locked by stale-state checks.
43. As a user, I want target changes, including change-back-to-original, to invalidate old edit tokens, so that the agent cannot overwrite intervening manual work.
44. As a user, I want world-position requests to detect relevant ancestor changes, so that they cannot use stale coordinate conversions.
45. As an agent, I want parent/child edits in one batch evaluated against one proposed final hierarchy state, so that entry ordering does not change the result.
46. As a user, I want identity and conflict tracking to cover normal viewer, scripting, and host mutations, so that safety does not depend on MCP being the only writer.
47. As a user, I want observers to see complete committed state, so that renderers and UI cannot observe half of a rearrangement.
48. As a user, I want one normal synchronization acknowledgement after a batch, so that success means the registered views can render the committed state.
49. As a user, I want committed changes reported honestly when synchronization fails, so that an error is not mistaken for rollback.
50. As an agent, I want fresh node versions and scene revision in committed results, so that I can inspect or edit again without guessing current state.
51. As a user, I want a Batch Reference for node edits, so that I can select the exact rearrangement for restoration.
52. As a user, I want restoration to recover original node state without SRT approximation, so that an imported transform returns exactly to its prior stored state.
53. As a user, I want restoration to reject later conflicting target or ancestor changes, so that recovery cannot overwrite newer human work.
54. As a user, I want unrelated node changes left intact during restoration, so that recovery is not a whole-layer rewind.
55. As a user, I want restoration to remain conditional and one-way, so that this feature does not silently introduce redo or unlimited undo.
56. As a viewer user, I want activity, commit status, and restore eligibility for placement edits, so that the new operations remain visible and controllable.
57. As a host developer, I want the same behavior headlessly and in the viewer, so that scene editing does not depend on ImGui, Lua, or Pi.
58. As a user, I want current read/edit restrictions enforced throughout the operation, so that readable data does not become automatically writable.
59. As an agent, I want identical request retries to replay rather than move pieces again, so that reconnects do not duplicate edits.
60. As a user, I want resource, cancellation, deadline, and retention limits preserved, so that larger inspection and editing capabilities do not create unbounded work.
61. As a user, I want the Fool's Mate workflow demonstrated entirely against one live scene, so that the first experiment no longer requires offline reconstruction.
62. As a maintainer, I want existing object-only clients and tests to remain compatible, so that broader editing does not regress the already working integration.

## Implementation Decisions

### 1. Feature boundary and existing contracts

- This is one feature increment with one milestone: broader inspection and
  edits of existing layer nodes. The completed integration milestone is not
  reopened. Camera/view editing, capture settings, and broader material/light/
  renderer write access remain deferred.
- The host-owned integration remains the primary module. Inspection, identity,
  edit preparation/commit, restoration, and activity modules are extended;
  layer mutation tracking is improved where needed. The MCP module remains a
  thin adapter over the public headless behavior. Scene access occurs only at
  host safe execution points, never directly on network threads.
- Existing launch authentication, single logical-controller ownership, lease,
  operation admission/replay, local authority, capability checks, deadlines,
  synchronization states, and retention limits are preserved. Handles and
  revision tokens grant no authority. Names and parameter strings are data,
  never executable instructions.
- Read permission on a registered scene permits the new inspection. Node
  editing uses that scene's existing read/edit grant and normal-sync hook. An
  exposed writable scene must explicitly opt into node editing; linking the
  new module or granting old object-parameter edits alone must not silently
  widen write exposure. Discovery advertises whether node editing is exposed.
  The viewer's explicit agent-integration mode opts in for this feature.
- Local host/viewer callers and authenticated external callers use the same
  serialized validation, commit, and restoration behavior. The node journal
  participates in the existing retention budgets and local Restore controls.
- There is no archive, Application Dump, imported-image orientation, or Studio
  client-server protocol change. Integration identities remain opaque and
  launch/scene-qualified, not Studio protocol pool-index identities. Internal
  identity/version machinery must not require persistent scene identifiers.

### 2. Inspection modes and filtering

- Extend `vsr_inspect` compatibly with a `kind` selector: `objects`, `layers`,
  or `nodes`. Omission means `objects`, retaining the existing object query.
  All three query one registered `scene_id` and remain admitted, paged
  operations using the existing Operation Reference/status pattern.
- Optional filters are exact `name`, substring `name_contains`, exact `handle`,
  object `object_type`/`object_subtype`, node `node_kind`, node `layer_handle`,
  and node `parent_handle`. Name matching is case-sensitive literal matching,
  not regex or glob evaluation; names are not unique identities. Combined
  filters are an intersection. `parent_handle` selects immediate children;
  omission permits all nodes in the selected layer(s).
- Object-only filters are invalid for layers/nodes, and node-only filters are
  invalid for objects/layers. Reject malformed or unsupported filters rather
  than ignoring them. A stale exact handle reports `conflict`; a well-formed
  name/type query with no matches succeeds with an empty complete result.
  A filter never grants access to an unreadable scene.
- Filter strings are bounded by the existing display-string limit, initially
  4096 UTF-8 bytes. Schema and tool descriptions disclose accepted enum values,
  query modes, and active limits. Exact matching compares the real stored name,
  not a truncated display preview; matching must yield in bounded work slices.
- Use deterministic order for an unchanged scene: existing object order,
  scene layer order, and per-layer node pre-order with stable sibling order.
  Filtering does not change identity or permit skipped state to be treated as
  inspected. A parent may be outside a filtered page; its handle remains
  resolvable by another authorized query.
- Retain current defaults: 64 items per page, maximum 256, 1 MiB per page,
  64 KiB per item, 16 KiB optional representation per item, 4096 visited
  entries per operation, 256 visits per safe-point slice, 8 MiB globally shared
  inspection scratch, and a five-second inspection deadline. These remain
  validated host-configurable limits, not separate unbounded allocations for
  hierarchy inspection. Reaching a visit budget returns the existing partial
  page/continuation semantics; a no-match scan must not loop indefinitely.
- Advertise the active page default/maximum and supported inspection modes in
  scene discovery and the MCP query schema/description. At default configuration
  `page_size: 500` is rejected before admission and the schema advertises 256.
- Bind each cursor to launch, scene, query kind, complete filter set, page-size
  policy, continuation position, and the relevant inspection revision. Changed
  returned state or query substitution produces `cursor_stale`; never combine
  stale pages into an apparent consistent snapshot. Identical request replay
  returns its historical page, not a fresh observation.
- Returned state, skipped-state accounting, cursor storage, matrix derivation,
  and filter traversal all consume bounded scratch/work budgets. Do not
  construct a complete unbounded hierarchy snapshot before paginating it.

### 3. Parameter values and reference inspection

- Existing object item identity/type/subtype/name/version and parameter
  name/type fields remain. Add a typed value representation to parameter
  entries and preserve the legacy editable `values` projection for old clients.
  Reading a value does not add the parameter to the edit allowlist.
- Support ordinary ANARI Boolean, integer, floating-point, string, numeric
  vector, matrix, and object-reference values. The declared ANARI type is
  authoritative; do not infer it from JSON shape. Numeric vectors preserve
  component order. Matrices use the row-array convention defined below.
- Boolean and string values remain Boolean and string. Finite float values
  round-trip their stored precision. Integer values outside JSON's exact
  safe-integer interval use a tagged decimal-string representation carrying
  the original integer type; do not silently round them into JSON numbers.
  Non-finite observed float components use explicit typed tags for NaN and
  positive/negative infinity, not nonstandard JSON numbers or fabricated zero.
  These tags are read representations, not accepted node edit inputs.
- Represent an object reference by its opaque object handle and declared type,
  with explicit null or unavailable/dangling states. Array references remain
  object links with summaries of element type, dimensionality, dimensions,
  and element count when available. Do not map/download array contents,
  dereference raw application pointers, or cause GPU work to inspect a value.
- Unsupported value types and optional representations that exceed budgets are
  marked with an explicit unavailable/omitted reason. Long strings may have a
  UTF-8-safe marked preview. Unsupported or truncated values never appear as
  complete, null, or zero. Identity, type, conflict tokens, and transform
  correctness fields are never truncated; if their representation cannot fit,
  report `item_too_large` using the existing query behavior.
- Inspection revision coverage now includes all returned parameter values,
  reference relationships, array summary metadata, and parameter structure,
  not just the five previously editable fields. Bulk array contents remain
  outside the representation and need not be hashed for inspection.

### 4. Layer/node observations and identity

- A layer item exposes its opaque handle, name, active flag, and root handle.
  A node item exposes opaque node/layer handles, parent handle or root marker,
  name, kind, own enabled flag, effective enablement, and a node edit-version
  token. An object-reference node also exposes the referenced object handle
  and type, including an unavailable state when the reference is invalid.
- Supported node kinds distinguish scalar affine transforms, object
  references, empty nodes, and transform arrays. A transform-array node exposes
  its array reference and summary; it is not represented as one selected
  instance matrix. Do not choose an arbitrary array element or instance.
- Transform observations carry the complete local matrix, derived world matrix
  when valid, local position, and SRT convenience values only when faithful.
  Explicit flags/reasons identify unavailable world transforms and unavailable
  rotation/scale convenience editing. Non-transform nodes inherit ancestor
  transforms but do not acquire writable transform storage implicitly.
- Effective enablement combines the layer's active flag and ancestor/node
  enabled flags. Editing a node's own flag does not modify layer activation or
  an ancestor's flag. Hidden nodes remain inspectable under read permission.
- Handles are issued by the host identity module, never supplied by clients or
  derived from a name, traversal ordinal, memory address, or raw pool slot.
  Handle domains distinguish objects, layers, and nodes. Wrong-kind handles
  cannot be accepted as an existing object or node target.
- Removal permanently invalidates the node/layer identity, even if a new node
  occupies the same slot, has the same name/value, or removal/recreation occurs
  between inspection calls. Copying a node creates a distinct identity. Moving
  the same live node does not redirect its identity, but invalidates affected
  structural/ancestry preconditions. Scene replacement invalidates its old
  layers and nodes. No identity is resurrected by restoration.
- Track editable node state separately from scene-wide inspection/render
  revisions. A node edit token covers its transform and own enabled state;
  relevant topology and ancestry have separately checkable dependencies.
  Renaming alone invalidates inspection cursors, not a transform edit against
  the same still-live node. Unrelated node changes must not advance every
  node's conflict version.
- Intervening target writes, including change-away-and-back, invalidate old
  tokens. A final-state fingerprint alone is insufficient to establish either
  lifetime or the absence of intervening edits. Track the normal mutation paths
  used by the viewer, Lua, animations, importers, and host code; document the
  required notifying mutation contract for custom hosts. Unsupported raw
  memory mutation is not a supported way to maintain integration correctness.

### 5. Node edit surface and compatible batch envelope

- Extend `vsr_edit`, not arbitrary scripting, with node-targeted entries in its
  existing scene-scoped `edits` list. Legacy entries remain object-parameter
  edits. A new `target_kind` discriminator is `object` or `node`; omission
  means `object`. Both use `handle`, `version`, `parameter`, and `value`.
  Node versions are opaque string tokens; existing object version encoding is
  unchanged. Wrong-kind/type tokens reject rather than coerce.
- Node parameter names are exactly `position`, `rotation`, `scale`,
  `transform`, and `enabled`. `position` accepts a three-component vector and
  `space` of `local` or `world`, defaulting to `local`. Rotation and scale
  are local-space only. `transform` is a complete local matrix. Do not accept
  world-space rotation, scale, or matrix replacement in this increment.
- `rotation` has explicitly named `azimuth`, `elevation`, and `roll` numeric
  components in degrees. `scale` is a three-component numeric vector.
  `enabled` is Boolean, not an integer flag. Only scalar-transform nodes accept
  transform-related edits. Every existing node kind can accept its own enabled
  flag; layer activation and root identity are not new edit fields.
- Object-reference, empty, and transform-array nodes reject transform edits;
  do not replace their payload, allocate a wrapper, modify referenced objects,
  or edit individual transform-array elements. Inspection must make eligible
  transform targets and parent relationships discoverable.
- Permit different fields on one node, with one consistent expected version,
  but reject a repeated field. A full `transform` entry is mutually exclusive
  with position/rotation/scale on the same node; it may coexist with `enabled`.
  Conflicting expected versions for one node are envelope errors. Preserve the
  existing prohibition on duplicate object/parameter entries.
- One batch can mix node entries with the already supported object-parameter
  entries in one registered scene. This extends the existing atomic batch,
  not the object edit allowlist. The existing maximum of 64 entries and all
  request/body/preparation limits apply to the combined list.
- Request identity includes target kind, handles, expected versions/dependencies,
  effective space defaults, and full authoritative parsed numeric content.
  Whitespace/member ordering and equivalent numeric spellings do not make a
  new request; different values that round to the same float32 still retain
  the existing canonicalization distinction. Identical retained requests
  replay without new mutation, allocation of a new batch, or scene-work slot.

### 6. Transform mathematics and validation

- The canonical node state is its full local affine float32 matrix. Wire
  matrices are four rows of four numbers, with column-vector mathematics:
  translation is in the last column and the affine last row is exactly
  zero, zero, zero, one. This wire layout is independent of VSR's internal
  column storage. Read and write paths must convert explicitly and consistently.
- Parse numeric edit inputs as finite binary64, validate the value before
  float32 conversion, reject out-of-range/overflow inputs, and use the existing
  correctly rounded binary32 conversion rules. Matrix last-row affine checks
  apply before rounding; reject projective matrices rather than rounding them
  into affine ones. Do not silently clamp numeric inputs.
- Local position replacement changes only the translation column and preserves
  every other stored matrix component. World positioning means the world
  location of the node's local origin, not its geometric center or bounds.
  Convert the requested point through the proposed final parent world matrix;
  replace only the resulting local translation. Ancestors' transforms and
  object data remain unchanged.
- The parent transform must be finite affine and invertible for world
  positioning. Reject invalid/singular parents or a conversion that cannot
  produce a finite representable local point. Never use a pseudoinverse or
  silently fall back to local coordinates. A valid singular local matrix can
  still be edited by local position or explicit full-matrix replacement.
- Rotation uses the existing VSR node convention: azimuth about local Y,
  elevation about local X, and roll about local Z, composed as azimuth then
  elevation then roll in VSR's matrix multiplication convention. The complete
  local matrix is translation multiplied by that rotation multiplied by scale.
  The observed convention and MCP descriptions must agree with the node editor.
- Rotation/scale conveniences are available only when the current local matrix
  can be reconstructed faithfully using its VSR SRT representation. Compare
  each linear-matrix component to the reconstruction with tolerance
  1e-5 times the maximum of one and that component's absolute magnitude;
  translation is preserved, not decomposed/reconstructed for a partial edit.
  Reject non-finite cached components or an unfaithful reconstruction. This
  rejects shear and any reflection/degeneracy not faithfully represented by
  the existing SRT state; do not assume decomposition alone proves fidelity.
- Replacement scale components are finite. Zero and negative components are
  accepted when the resulting SRT/matrix state remains faithfully representable;
  do not impose a positivity rule absent from VSR's node model. A singular
  result still cannot serve as an invertible parent for a world-position edit.
  Full-matrix editing remains the explicit route for otherwise non-SRT affine
  state. Rotation accepts finite angles
  within float32 range. Convenience edits retain supplied degree values using
  VSR's SRT behavior; do not wrap them before request-identity comparison.
  Matrix-derived observations may use VSR's normalized angle representation
  and do not promise a unique Euler decomposition. No world-space SRT
  approximation is introduced.
- A grouped partial edit first determines the proposed linear part from local
  rotation/scale changes, then determines its local translation from local or
  world position, and applies enabled state independently. Entry order is not
  execution order. The same grouped values produce the same proposed state.
- For parent/child edits in one batch, compute proposed parent matrices before
  converting child world positions. Each child world request therefore applies
  to the proposed final ancestors, not whichever entry happened to run first.
  All dependency validation is against the inspected pre-commit state. Cycles
  cannot be introduced because reparenting is not an edit in this increment.

### 7. Conflict detection and commit

- Use a narrow dependency model: all node entries require the target's current
  identity, editable version, layer membership, and structural parent chain.
  World-position entries additionally require the inspected transform state
  of the ancestor chain used for conversion. Inspection returns an opaque
  `ancestry_version` dependency token; world-position entries must include it.
  Missing/malformed required tokens reject before admission; mismatching live
  versions/dependencies report `conflict` at the safe point.
- Local transform edits do not conflict merely because an unchanged live parent
  was translated or rotated; their local meaning is unchanged. Reparenting,
  ancestor removal, or identity replacement conflicts. World-position edits
  conflict on ancestor transform writes, including change-back. Unrelated
  sibling changes, other layers, names, or object parameters are not node edit
  dependencies unless those objects are also targets of the same batch.
- The node token is conservative within one node: any intervening transform or
  own-enabled write conflicts with a stale node edit. Structural checks must
  detect every relevant change without rejecting unrelated structure elsewhere
  in the layer. Inspection cursors may be broader than edit tokens.
- Existing object entries retain their object-level parameter conflict rule.
  A mixed batch validates every object and node target/dependency together.
  Fail permission, target, value, dependency, or preparation checks before any
  scene mutation, version advancement, visible notification, or committed
  journal publication.
- Prepare complete final node states, object values, dependency information,
  notification storage, and bounded restoration payload before commit. No
  fallible allocation or decomposition may discover a rejection halfway
  through mutation. A preparation capacity failure reports `busy` unchanged.
- Commit all prepared state before sending normal scene notifications. Notify
  affected layers through the existing scene delegate system, coalescing work
  per affected layer; never bypass RenderIndex or UI synchronization directly.
  Observers that read during notification see the complete batch, not an
  intermediate parent/child or object/node state.
- Advance targeted versions and scene/inspection revisions normally. Do not
  rebuild every node identity or invalidate unrelated node edit versions merely
  to publish a layer notification. Preserve reset/default transform behavior;
  editing a placement does not overwrite its stored default baseline.
- Use the scene's normal synchronization hook once after edit/restore commit.
  Terminal success requires acknowledgement for the scene's registered views.
  Failure retains the committed scene and returns `applied_with_sync_failure`,
  with a Batch Reference and restore eligibility. A previously failed scene
  remains failed until explicit Resynchronize, as in the existing integration.
  Cancellation or disconnect after commit never reports a non-commit.

### 8. Restoration, results, and activity

- A committed node or mixed batch publishes one Batch Reference, not a second
  journal. Include committed status, resulting scene revision, existing object
  `versions`, and an additional `node_versions` list carrying node handles,
  current edit versions, and updated ancestry dependency tokens. Object-only
  results retain their existing shape/meaning; node-only results need no
  invented object parameter-version tokens.
- Restoration stores the exact pre-edit node matrix, own-enabled state, and
  transform representation needed to preserve public SRT/default behavior,
  together with only the fields the batch changed and its post-commit version/
  dependency information. Do not restore a whole layer, referenced geometry,
  names, default baselines, unrelated parameters, or unedited nodes.
- Validate live target identities, post-commit node/object versions, membership,
  and required ancestry before restoring. Later writes to an affected node
  conflict even if its value was changed back. Unrelated node changes do not.
  For world-position edits, later changes to required ancestor transforms also
  conflict: restoration must not reinterpret the recorded move under a new
  hierarchy. Local edits have the narrower dependency rules above.
- Ancestors edited by the same batch are validated against their recorded
  post-commit state, not against the batch's earlier inspection token. Restore
  parent and child prior state atomically. This must not make an otherwise
  untouched parent/child batch immediately unrestorable after its own commit.
- Preparation failures leave values, versions, and restore eligibility unchanged.
  Preserve the current integration's conflict policy: an actual restore
  identity/version/dependency conflict leaves scene values unchanged, permanently
  marks the batch conflicting, and releases its restoration payload. Later
  attempts still return `conflict`; they do not revive eligibility.
- Successful restoration consumes eligibility and advances versions normally,
  even if downstream synchronization then fails. It creates no redo record.
  Preserve `already_restored`, expired/unknown `outcome_unavailable`, and
  identical restore-request replay behavior. A newly attached authorized session
  and host-local Restore may select retained batches under existing authority.
- Count node states, ancestry/dependency records, and mixed payloads in existing
  activity/restoration byte budgets and restorable-batch limits. Never evict
  storage required by active work. No unbounded ancestry snapshots or permanent
  per-session histories are introduced.
- Activity shows operation kind, committed/no-commit outcome, affected scene,
  Batch Reference, and restoration eligibility for node and mixed edits.
  Existing Disable agent edits, Disconnect, Restore, and Resynchronize controls
  continue to work; no new chat interface is required.

### 9. Outcomes and compatibility

- Retain the existing outcome vocabulary and carrier. Envelope/type/unsupported
  field failures are `invalid_request`; stale live handles, tokens, or ancestry
  are `conflict`; current permission/exposure failures are `forbidden`.
  Query cursor substitution is `cursor_stale`; representation overflow uses
  `item_too_large`; bounded admission/preparation failures use `busy`.
- Node value failures include a stable structured reason distinguishing
  unsupported node kind, malformed/non-finite/out-of-range input,
  non-affine matrix, unrepresentable SRT convenience edit, singular parent,
  and conflicting transform fields. Return the failed entry index where
  applicable and explicit `committed: false`; clients must not parse English.
- Additive query/result fields and explicitly discriminated node edit entries
  preserve existing object-only calls, tools, object parameter validation,
  operation replay, synchronization, restoration, and permissions. No new
  tool depends on arbitrary Lua, filesystem access, or shell execution.
- Update user-facing tool descriptions/documentation to distinguish scene
  objects from layer nodes, local/world position, rotation convention,
  unavailable values, edit exposure, and actual paging limits. Do not imply
  that editing a stored camera now controls the viewer or agent capture camera.

## Testing Decisions

### Primary seam

Use the already agreed public headless integration interface with real VSR
Scenes and a host-owned safe-point loop. This is the single primary behavioral
seam, consistent with the existing integration spec and the approved decision
to preserve its safety behavior. No new test-only interface or second product
seam is required. MCP and viewer tests are thin integration checks, not another
implementation of the business rules.

Good tests observe query results, public scene state, delegate-visible complete
commits, operation outcomes, versions, permissions, synchronization, and
restoration through public interfaces. Do not assert private map layouts,
internal identity allocation order, cache organization, traversal algorithms,
or helper call counts that are not part of the public contract. Use independent
worked matrices and literal expected coordinates rather than recomputing the
implementation in the assertion.

### Behavioral coverage

1. **General inspection:** real cameras, renderers, non-editable materials,
   scalar/vector/string/matrix parameters, safe and large integer types,
   non-finite observed floats, valid/null/dangling object references, and array
   summaries. Reading must not widen object edits or map bulk arrays.
2. **Hierarchy:** multiple layers, inactive layers, enabled and disabled
   ancestors/children, duplicate names, shared surfaces in multiple placements,
   object/empty/transform-array nodes, local/world matrices, and node-kind edit
   affordances. Query one placement without confusing its shared object.
3. **Filtering and pagination:** default/maximum advertisement, every supported
   filter and invalid combination, exact versus substring case-sensitive
   matching, no matches, duplicate-name matches, direct handles, parent/layer
   scoping, continuation binding, changed query/cursor rejection, and changed
   parameter/node fields staling cursors. Bound sparse/no-match and deep-tree
   work; exercise cancellation and scratch release on every terminal path.
4. **Identity and ABA:** remove/recreate same-name, same-value nodes in reused
   slots; copy, host reparent, ancestor removal, layer removal, scene reset, and
   replacement between inspections. Old handles never redirect. Metadata-only
   rename stales inspection but leaves an otherwise valid node edit usable.
5. **Matrices and conveniences:** asymmetric row-array matrix oracle, explicit
   VSR degree rotations, nonuniform scale, faithful signed-scale cases, shear,
   singular local matrices, unfaithful SRT caches, non-finite and overflowing
   inputs, and non-affine/projective rejection. Position-only edits preserve
   every linear component. Unsupported rotation/scale leaves everything intact.
6. **World positioning:** parents with translation, rotation, and nonuniform
   scale; transformed grandparents; singular/invalid parents; the node origin
   versus geometric center; and parent/child edits in both entry orders.
   Expected world positions come from independent worked examples. Dependencies
   validate inspected state while conversion uses proposed final parents.
7. **Fine conflicts:** unrelated pawn moves do not block queen local edits or
   restoration; target transform/enabled change-back invalidates old tokens;
   reparenting/identity replacement conflicts; a parent transform change blocks
   stale world positioning but not an unrelated local-space edit. Parent changes
   within a committed batch do not immediately invalidate its restoration.
8. **Batch correctness:** all supported node fields, duplicate/conflicting
   entries, wrong-kind handles/tokens, maximum combined entry count, valid mixed
   object/node batches, and one stale/invalid entry preventing every mutation.
   Inject preparation failures before commit and verify unchanged values,
   versions, notifications, eligibility, and absence of a committed Batch
   Reference. Observers must see all final values at their first notification.
9. **Synchronization and authority:** read-only and non-node-edit-exposed scenes,
   edit-disable before execution, disconnect/cancel before and after commit,
   identical retries, host-local edits, ordinary hook failure, pre-existing
   failed scene, and explicit Resynchronize. Confirm honest commit status and
   blocked renders after sync failure without widening authority.
10. **Restoration:** exact original matrices and enabled state, shear/reflection
    where matrix edits are supported, unedited names/default baselines,
    parent/child and mixed batches, subsequent target/ancestor conflicts,
    unrelated edits surviving, preparation failure retaining eligibility,
    permanent conflict, successful consumption, repeated restore, expiration,
    cross-session selection, and local Restore after Pi disconnect/edit-disable.
11. **Retention and resources:** restoration payloads share existing budgets,
    active work is protected, large dependency chains fail or yield boundedly,
    and neither expanded parameter values nor hierarchy tracking introduces
    unbounded request scratch or retained journal data.
12. **Regression:** all existing object-only inspection/edit/restore, identity,
    synchronization, MCP, rendering, and viewer tests continue to pass. Existing
    parameter allowlists, revisions, lease/credential behavior, and image
    contracts are unchanged.

### Acceptance workflow and prior art

- Use a procedural chess-like fixture with shared piece objects, distinct named
  transform nodes, and a board parent carrying a non-identity transform. Freeze
  its coordinate mapping independently of the editing implementation. Inspect
  and identify the white f-pawn, white g-pawn, black e-pawn, and black queen;
  submit one batch for f3, g4, e5, and h4; inspect the resulting placements; then
  restore the Batch Reference and verify exact prior state. Other 28 pieces,
  shared objects, parent state, names, and defaults remain unchanged.
- Repeat the same essential inspect/edit/restore workflow through the MCP
  adapter on a running host. No shell, raw scene pointer, source-reading,
  offline asset reload, or Lua execution may be used to discover or perform
  the moves. A tool-capable model may drive a manual smoke run, but automated
  acceptance uses deterministic calls rather than judging model taste.
- Add a viewer smoke check using the imported chess scene when its assets are
  available: integration-enabled viewer, placement inspection and edits,
  truthful Agent Activity, visible live rearrangement, and restoration. The
  procedural fixture is the portable automated oracle; missing optional asset
  files are not a reason to skip its acceptance.
- Reuse prior tests for opaque identity lifetime, bounded inspection/cursors,
  atomic Agent Edit Batches and observer consistency, one-way Batch Restoration,
  synchronization failure/recovery, and real loopback MCP transport. Existing
  layer-node/editor behavior is the rotation/default-state prior art.
- Use CPU-capable rendering only for a thin visual/synchronization check when
  required; transform correctness does not require GPU convergence. Build and
  install freshly changed libraries before tests where the installed-library
  search path takes precedence. Exercise integration/MCP enabled and the
  unrelated build configurations with integration or MCP disabled.

## Out of Scope

- Camera/view editing, temporary cameras, queen's-eye capture, custom resolution,
  sample count, denoiser settings, image-file export, or background parity fixes.
- Broader physically based material, HDRI, renderer, or camera parameter edits;
  reading their ordinary values is in scope, writing them is not.
- Arbitrary live Lua/script execution, shell commands, source modification,
  filesystem browsing, dataset import, or offline scene reconstruction.
- Node/layer/object creation or deletion, reparenting, implicit transform
  wrappers, changing node/object references, editing transform-array elements,
  or changing layer activation through the new edit surface.
- World-space rotation/scale/matrix replacement, automatic shear removal,
  pseudoinverse positioning, or silently repairing malformed transforms.
- Scene/node bounds queries, asset-path provenance, ANARI Library/device
  discovery improvements, and bulk array exports; these remain separate gaps.
- Whole-scene transactions, whole-layer undo, forced/partial restoration, redo,
  persistent journals, cross-restart handles, or crash-consistent editing.
- Automatic attachment, remote/browser-origin control, multiple controllers,
  weaker permission checks, or scene access on transport threads.
- Shared result-store extraction, FlatMap migration, unrelated architectural
  cleanup, archive-schema changes, and Studio client-server protocol changes.
- Publishing local working notes, specs, or tickets to GitHub without explicit
  authorization.

## Further Notes

- This is the local issue-tracker specification, labeled `ready-for-agent`.
  That label means the feature is specified for implementation; it is not proof
  of implementation, successful validation, or an independent review gate.
- [Design notes](design-notes.md) record the user-approved scope and checked
  facts. [Experiment gaps](../vsr-mcp-gaps.md) and the
  [Fool's Mate workaround](../scripts/fools_mate.lua) provide motivation and
  coordinate examples, not hidden requirements. The experiment's screenshot
  workflow is deliberately not the acceptance target of this increment.
- Existing integration behavior is described by its [specification](../pi-integration/spec.md)
  and approved [addenda](../pi-integration/addenda/). In particular, this spec
  preserves the [permanent restoration-conflict policy](../pi-integration/addenda/01-restore-conflict-permanence.md)
  rather than accidentally reviving the earlier, superseded eligibility rule.
- The primary headless test seam was already confirmed in the preceding
  integration design and retained in this discussion. No additional interview
  or new seam approval is needed to synthesize this specification.
- Matrix encoding, filter syntax, mixed-batch support, dependency tokens, and
  numerical fidelity thresholds above are concrete technical completions of
  the approved behavior. They were synthesized here, not independently
  prototyped or claimed as already implemented.
- Treat "placement" as a user's description of the effect of layer nodes,
  not a new scene-object type. A named transform node may parent one or more
  object-reference nodes; edits address the actual existing node explicitly.
- No implementation tickets or code changes are created by this specification
  step. A later ticket-cutting step can divide work into inspection/identity,
  edits/conflicts, and restoration/acceptance while preserving the complete
  contract and its one primary test seam.
