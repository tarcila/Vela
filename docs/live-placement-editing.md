# Inspect and rearrange a live scene

Build with `VSR_USE_INTEGRATION=ON` and `VSR_USE_INTEGRATION_MCP=ON`, then
build **and install** before testing if installed libraries precede build-tree
libraries in your environment. Start the viewer explicitly:

```bash
./_out/_cmake/vsrViewer --agentIntegration -gltf /path/to/scene.gltf
```

Without `--agentIntegration` the viewer does not expose agent control. Connect
one trusted client through `vsrMcpProxy` using the descriptor path shown in
**Agent Activity**. The proxy handles credentials privately; do not copy the
connection file's contents into a prompt. See [attachment](pi-attachment.md)
and the [MCP carrier contract](../src/vsr/integration/mcp/README.md).

The examples below are **tool arguments**, not shell commands or live scripts.
Replace placeholders with observations from this running application. Each
admitted operation returns `structuredContent.operation_ref`; poll
`vsr_operation_status` with `{"operation_ref": <that reference>}` until
`state: "terminal"`. Read `outcome` and `committed` independently. Use a fresh,
increasing integer `request_id` for each new operation (1–9007199254740991).
An identical retained retry replays history; changing arguments under the same
ID yields `request_id_conflict`.

## Discover, filter, and page

First call `vsr_discover`. A scene item has `kind: "scene"` and an integer
**`id`**; copy that `id` into subsequent requests' `scene_id`. A view item
likewise supplies `id` for `view_id`. Scene items advertise
`node_edits_exposed`, and `inspection.kinds`, `default_page_size`, and
`max_page_size`. Readable does not imply writable: node edits require explicit
host exposure **and** a current scene edit grant. `edit_affordances` describe
representation, not permission.

```json
{"request_id":2,"scene_id":0,"kind":"nodes","name_contains":"Queen","node_kind":"transform","page_size":64}
```

- Omitted `kind` means `objects`; other kinds are `layers` and `nodes`.
- All kinds accept exact `name`, case-sensitive literal `name_contains`, and
  exact opaque `handle`. Names need not be unique; do not edit by name.
- Objects also accept `object_type` (advertised ANARI enum string) and exact
  `object_subtype`. Nodes also accept `node_kind` (`empty`, `transform`,
  `object`, `transform_array`), `layer_handle`, and `parent_handle` (immediate
  children only). Filters intersect. Filters for a different kind reject.
- Default limits are 64 items, maximum 256, 1 MiB/page, 64 KiB/item,
  16 KiB optional representation/item, and 4096 UTF-8 bytes/filter. Use the
  **active** schema/discovery limits; `page_size: 500` rejects at defaults.
- Pages expose `complete`, `stop`, `visited`, `returned`, `name_work`, and
  `cursor`. An empty incomplete page is not proof of absence: bounded traversal
  or name matching may stop on work before finding a match. Default traversal
  limits are 4096 visits/operation and 256/safe-point slice; name comparison
  limits are 65536/operation and 4096/slice. Matching uses the real stored name,
  even beyond a truncated preview.
- Continue with a **new** request ID, the returned cursor, and the identical
  scene/kind/filters/effective page size. Never substitute a query. Cursors bind
  revisions; on `cursor_stale`, restart rather than merge incompatible pages.
  Each query has a five-second deadline and shares bounded inspection scratch.

## Objects are not placements

An object owns ANARI parameters; a node places or references an object in a
layer hierarchy. Several nodes may reference one shared surface. Editing a
shared object's supported parameter affects all its placements; moving a
transform node moves that placement/subtree only. Object, layer, and node
handles are distinct, launch/scene-qualified lifetimes. Removal/recreation
never redirects a stale handle, even with the same name or pool slot.

Node records expose `parent_handle`, `layer_handle`, `node_kind`, own `enabled`,
`effective_enabled` (layer plus ancestors), and dependency tokens. Hidden nodes
and inactive layers remain readable. Follow parent handles to find an existing
scalar transform when an object-reference node has no transform storage.
Transform arrays expose array metadata, not an arbitrary instance matrix.

Names are display records (`{"text":"Queen_B","truncated":false}`), not
bare strings. A transform's `local_matrix` and `world_matrix` are typed
representations; read their `state` before accessing row-array `data`.
`local_position`, `rotation`, and `scale` likewise wrap observations in
`state`/`data`. Observed rotation data is `[azimuth,elevation,roll]`; the write
value below instead uses explicitly named components. Affordances use
`available` and an optional `reason`; matrix availability is advertised as
`edit_affordances.matrix`, while its write parameter is `transform`.

Object parameter entries have declared ANARI types, typed `value`
representations, an `editable` Boolean, and typed `min`/`max` values and
`string_values` choices when present. `editable` describes supported value
editing, not an authority grant. The legacy `values` mirror covers generic
supported float32 scalar/vec2/vec3/vec4 values; it is not exclusive edit
authority. Inspect camera, renderer, material, geometry, and reference
parameters without gaining writes. References distinguish valid opaque links,
null, and unavailable/dangling state.
Arrays provide element type, dimensions, and counts, never downloaded contents.
Large integers use lossless decimal tags and observed non-finite numbers use
explicit tags. `state`, omission/unavailability reasons, and marked string
previews must not be mistaken for complete values, zeros, or null references.

## Edit with the observed tokens

Copy a node's `node_edit_version` into the edit entry's **`version`** field.
Copy `structural_version` verbatim for **every** node entry. For
`parameter: "position", space: "world"`, also copy `ancestry_version`.
The parser enforces these conditional requirements even though the additive
schema lists the dependency properties without making them globally required
(object entries do not use them). Do not parse or invent opaque tokens.

```json
{
  "request_id":3,
  "scene_id":0,
  "edits":[{
    "target_kind":"node",
    "handle":"<node.handle>",
    "version":"<node.node_edit_version>",
    "structural_version":"<node.structural_version>",
    "ancestry_version":"<node.ancestry_version>",
    "parameter":"position",
    "space":"world",
    "value":[1.0,0.0,2.0]
  }]
}
```

For a local position entry, omit `space` (or set `"local"`); ancestry is not
required. Position means the node's **local origin**, not its geometric center
or bounds. Local/world positioning replaces only translation, preserving the
complete linear part, including shear, reflection, or singular local state.
World conversion uses finite, affine, invertible **proposed final ancestors**;
singular parents reject without pseudoinverse or local fallback.

Only scalar-transform nodes accept transform fields:

| `parameter` | `value` | Space |
|---|---|---|
| `position` | `[x,y,z]` | local (default) or world |
| `rotation` | `{"azimuth":90,"elevation":0,"roll":0}` | local only |
| `scale` | `[sx,sy,sz]` | local only |
| `transform` | four rows of four numbers | local only |
| `enabled` | `true` or `false` | own flag; any existing node kind |

Rotation is VSR **degrees** about Y (azimuth), X (elevation), then Z (roll),
with translation × rotation × scale composition. Supplied angles are retained,
not wrapped. Convenience rotation/scale require faithful cached SRT
reconstruction within `1e-5 * max(1, abs(component))` per linear component;
shear/unfaithful state rejects, never silently repairs. Faithful signed/zero
scales are allowed. Inspect availability/reasons before using conveniences.

Matrices use column-vector mathematics despite their JSON row-array layout;
translation occupies the last column. The affine last row must be exactly
`[0,0,0,1]` **before** float32 rounding. For example:

```json
[[1,0,0,3],[0,1,0,4],[0,0,1,5],[0,0,0,1]]
```

All node edit numbers must be finite binary64 and float32-representable; no
clamping or non-finite read tags are accepted as write inputs. Full affine
node matrices may contain shear, reflection, or singular linear parts.

One batch has 1–64 combined entries, possibly mixing supported object and node
writes in one scene. Object entries default `target_kind` to `object` and use
an integer object `version`; node versions are strings. Different node fields
share one expected version/dependency set. Repeated fields reject; `transform`
cannot combine with position/rotation/scale, but may combine with `enabled`.
Grouping is order-independent: final linear parts precede position conversion,
and child world positions use final parents even when the child entry comes
first. One invalid/stale entry prevents all mutation. Preparation is bounded
(including at most 4096 ancestor steps/batch); exhaustion returns `busy` unchanged.

### Object parameter values

Object edits use generic introspection-driven validation of an **existing**
parameter's stored type and metadata, not a material/light allowlist. Copy the
object's inspected `handle` and integer `version`; use the parameter's name
and a value matching its stored type:

| Stored type | JSON write value |
|---|---|
| `BOOL` | Boolean (`true` or `false`) |
| `STRING` | String without embedded NUL; must match a `string_values` choice when choices are present |
| Signed/unsigned 8-, 16-, 32-, or 64-bit integer scalar or vec2/vec3/vec4 | Integral JSON number(s) within both the stored type's range and `[-(2^53-1), 2^53-1]` |
| float32/float64 scalar or vec2/vec3/vec4 | Finite JSON number(s) within the stored type's range |

Scalars take one value; vectors take arrays of exactly the stored component
count. Matching `min`/`max` metadata is enforced per component, and `COLOR`
usage retains the `[0,1]` domain. Parameter `enabled` is preserved by value
edits and does not restrict them: a disabled parameter's supported value can
still be edited. References, matrices, and all other stored types are
unsupported, and edits cannot create parameters. Readable values, including
lossless integer tags and non-finite read tags, are not automatically writable.
Stored camera parameter edits do not control the viewport or agent capture
camera. The existing authority, atomic batch validation, object versioning,
and conditional restoration rules remain unchanged.

## Preview and commit a selection translation

Use `vsr_translate_preview` for a finite **world-space displacement** of an
explicit selection. Copy all three fresh node dependency tokens for every
entry, including selected descendants:

```json
{"request_id":10,"scene_id":0,"selection":[{"handle":"<node.handle>","node_edit_version":"<node.node_edit_version>","structural_version":"<node.structural_version>","ancestry_version":"<node.ancestry_version>"}],"world_delta":[0.1,0,0]}
```

Poll `vsr_operation_status`. Preview writes nothing and creates no Batch
Reference or restore eligibility. Its result reports current/predicted origins,
exact prepared local matrices, topmost selected roots and inherited selected
nodes. Identical duplicate entries collapse. Selecting both a parent and child
moves both origins once through inheritance, without editing the child's local
matrix. Selecting siblings never selects their unselected common parent.
Unselected descendants inherit naturally and are explicitly **not enumerated**.
Origins are not geometric/bounds centers. Linear transforms, including shear,
reflection and supported local zero scales, are preserved; required singular
parents reject. Ordinary fractional inputs use float32 rounding, not exact
binary64 round-trip rejection.

Commit by copying the **entire returned `preview_ref`** and using a new request
identity; do not supply replacement selection, matrices or translation inputs:

```json
{"request_id":11,"preview_ref":{"launch_id":"<result.preview_ref.launch_id>","preview_id":"<result.preview_ref.preview_id>","scene_id":0}}
```

The reference's `scene_id` is routing metadata, not an authority grant. The
server binds the exact plan to its launch, session, Scene and original selection.
Immediately before mutation it rechecks current read/edit/exposure, lifetime,
all selected identities (including inherited nodes), ancestry and normalization.
Relevant changes reject atomically rather than silently rebasing; unrelated
edits remain valid. Failed commits create no usable batch. A successful commit
consumes the plan once and returns the normal edit receipt/`batch_ref` for
`vsr_restore` below. Another request cannot reuse the plan; the same admitted
request replays historical metadata without another write, even after restore.
Expired, evicted and foreign references return `outcome_unavailable` rather than
reviving a plan. Current read withdrawal suppresses protected historical results.
Before-commit cancellation writes nothing; late cancellation is not rollback.
Sync failure can still be committed, so inspect both `committed` and `outcome`.

Default bounds remain 64 input selections, 8 plans/session, 32 plans globally,
16 MiB aggregate owned charges, 4096 ancestry steps, 120 seconds plan lifetime
and a 5-second operation deadline. Hosts may configure validated narrower
policies; the tool advertises its actual policy. Historical metadata and held
status copies remain charged until their real record/copy lifetimes end.

## Conflicts, synchronization, and restoration

Refresh by exact handle before editing again. Target transform/enabled writes,
even change-away-and-back, invalidate old versions. Reparenting/ancestor lifetime
changes invalidate structural tokens. Ancestor transform writes invalidate world
ancestry, but not an otherwise current local-space edit. Renames stale inspection
cursors without invalidating local edit tokens; unrelated sibling edits do not
lock the whole layer.

A committed result supplies one `batch_ref`, `scene_revision`, object `versions`,
and `node_versions` with fresh `version`, `structural_version`, and
`ancestry_version`. Notifications expose complete state and one normal scene
synchronization acknowledgement. `applied_with_sync_failure` means **committed**,
not rollback: keep the batch reference. Rendering stays blocked until explicit
`vsr_resynchronize` succeeds. Cancellation/disconnection after commit does not
undo the batch.

```json
{"request_id":4,"scene_id":0,"target_batch_ref":{"launch_id":"<result.batch_ref.launch_id>","batch_id":"<result.batch_ref.batch_id>"}}
```

Submit that to `vsr_restore`, then poll its own operation reference. Restoration
is conditional and one-way: exact prior changed matrices/cached representation
and own-enabled state return, not a whole-layer rewind. Names, reset defaults,
shared object data, and unrelated edits survive. Later target or required
ancestor writes conflict; a genuine restore conflict permanently consumes
eligibility, even after change-back. Successful restoration consumes eligibility
(`already_restored` on a new attempt), while identical retries replay. Expired
or evicted outcomes return `outcome_unavailable`; there is no persistent journal,
forced restore, partial restore, redo, or unlimited undo.

In **Agent Activity**, check operation kind, queued/terminal status,
committed/not-committed outcome, scene, batch reference, and eligibility. The
local **Restore** button selects that specific retained batch; local recovery
remains available after client disconnect or **Disable agent edits**. Existing
**Disconnect** and **Resynchronize** controls do not widen write permissions.

## Deliberate boundaries

Object writes are limited to existing parameters with the supported stored
types and metadata described above; reading a parameter never grants writes.
There is no parameter creation or object-reference/matrix parameter editing,
nor new creation/deletion/reparenting, reference replacement, layer activation,
implicit wrapper, or transform-array element edit.
Stored scene camera inspection/editing does **not** control the interactive
viewport or private offscreen agent camera. Camera/view editing, queen's-eye
views, capture settings, custom resolution/sample count/denoiser, bounds/path
queries, arbitrary live Lua/shell execution, and server-side image-file export
remain deferred. Existing rendering returns MCP PNG content at view-owned size. To render an
existing view, copy **`scene_revision` from an inspection page** and
`view_revision` from its discovery view item into `vsr_render`. The page's or
discovery's aggregate `revision` is a cursor-binding token, not a render token.
Use `vsr_render_image` with the render's operation reference for PNG content;
refresh both tokens after edits rather than guessing them.

Custom hosts use the same headless behavior at safe execution points. Use
tracked `LayerNodeData` setters and publish the appropriate Scene layer signal
(coalesced signals are supported). Raw memory writes cannot maintain lifetime,
change-back, cursor, or renderer correctness. No ImGui, Lua, or Pi dependency is
required by the headless integration library.

## Portable acceptance

`vsrIntegrationTests '[chess]'` and `vsrIntegrationTests '[MCPchess]'` exercise
32 shared-object placements under a transformed board: inspect white f/g pawns,
black e-pawn and queen; commit f3, g4, e5, h4 in one batch; inspect; restore exact
prior state. The other 28 placements and shared/default state remain unchanged.
The MCP case uses real authenticated loopback transport and host safe points.
Imported assets are an additional viewer smoke check, not a substitute for
this deterministic public-interface oracle.
