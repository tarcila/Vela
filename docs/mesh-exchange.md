# Agent Mesh Exchange and typed working bundles

`vsr_mesh_export(request_id, scene_id, handle)` exports one currently exposed
triangle Geometry with Scene READ (EDIT is not required). Poll ordinary
operation status for kind `mesh_export`, `file_name`, `target_version` and
`dependencies`. The descriptor's Image exchange metadata additionally publishes
`mesh_export`, qualified `mesh_import`, `mesh_exchange_directory` and finite
mesh limits. Both formats share the same launch-private directory and quotas.
Mesh Import is exposed only when the host binds normal restoration, authoritative
retention and physical reservations, in addition to owning-thread Array tracking.
Mesh Import supports coherent **position, topology and attribute replacement**,
including different vertex/triangle counts and optional binding presence/state.

The receipt is historical: replay does not rewrite files or certify their
current contents. Export never maps, changes Arrays/bindings, advances versions,
or allocates restoration retention. Exported files remain working copies until
explicit removal or launch cleanup. They are not an undo store. Preserve the
freshness evidence separately when processing independently authored bundles.
Evidence tokens are equality-only and scoped to Geometry identity/parameter
state and the bound Arrays' identity/lifetime/data state, not a global revision.

## Architectural boundary and discovery

The revisioned envelope is deliberately **not an Object Archive**. Object
Archive deserialization adds a dependency closure; Mesh Import must instead
validate and replace only permitted bindings of one existing Geometry. The
flat JSON-plus-raw-bytes carrier makes actions, alias intent and decoded byte
cost inspectable without granting authority to resolve object references or
external paths. It neither extends core Data Tree encoding nor changes native
Scene/Object Archive compatibility (see ADRs 0011 and 0026). Normal persistence
is documented in [mesh-exchange-persistence.md](mesh-exchange-persistence.md).

Mesh files reuse ADR 0042's private `images/` directory with a `.vmesh` filename
namespace, not a second filesystem permission model. Image `.png` behavior is
unchanged; the two namespaces share file/byte quotas and physical/undo budgets.
Only qualified Linux hosts with safe directory handling and owning-thread write
tracking advertise export; import additionally needs the normal recovery and
reservation callbacks. Consult authenticated `vsr_discover`, the exchange
metadata and MCP `tools/list`; absence is not permission to use a private API.
`mesh_import: true` describes host qualification, not a bypass of current Scene
READ+EDIT, exposure or enabled agent edits. READ-only sessions may export.
Discovery's usage/capacity observations can change before admission; they are
not reservations. Export supplies the current complete mesh dependency tokens.

## Revision 1 representation

A `.vmesh` is one self-contained, uncompressed file:

1. Eight-byte magic `VMESH01\n`.
2. Little-endian uint32 UTF-8 JSON header length.
3. That many header bytes.
4. Exact Array bytes, in `arrays` declaration order, with no alignment padding.

The header has `revision: 1`, `byte_order: "little"`, `fields` and `arrays`
objects. Each field is keyed by its Geometry parameter name and declares
`action` (`preserve`, `replace`, `remove`), `enabled`, `association` and a
bundle-local `payload` identifier unless removed. Arrays declare
`element_type` (ANARI spelling), `count` and exact `byte_length`. Export includes
`source` descriptive target/dependency evidence. Source metadata does not grant
Scene authority. No arbitrary object closure, reference Array, path, compressed
member, Material, Image, Layer or external dependency is representable.
The core Data Tree and native Archive formats remain unchanged.

Export declares every present supported field as `preserve`, with its payload
available for inspection. Omission never means removal. `replace` supplies a
payload; optional `remove` is explicit; positions cannot be removed. Repeated
payload IDs preserve aliases. Independent Arrays receive distinct IDs even
when their bytes match. Aliases do not imply equal associations: each field
keeps its own association and enabled state. A helper write updates the shared
payload and marks **every alias** `replace`; use `replace` on just one field to
split it deliberately, even when its bytes are equivalent. Untouched helper
processing preserves bytes, types, actions and alias intent. `replace` on an
absent optional field is an addition; no separate `add` action is needed.
Omission retains the live binding. Explicit `preserve` requires an existing
binding with the declared enabled state and exact type/count/bytes. Conflicting
preserve aliases reject rather than invent a split or merge. A replacement can
explicitly alias a preserved field only with an equivalent payload; its preserved
binding remains the anchor. All other changed payload groups use independent,
right-sized new Arrays. Equivalent full alias-group replacements are no-ops;
byte equality alone never merges independently declared Arrays.

Revision 1 is the only supported revision and little-endian is the only
supported byte order. Unknown/incompatible revisions, unsupported fields/types,
ambiguous declarations and malformed lengths are refused; there is no automatic
migration, implicit coercion or geometry repair. The external helper is a working
file authoring tool, not the privileged validation boundary: successfully loading
or saving a helper bundle does not imply that the host will accept its complete
proposed Geometry.

### Compound Mesh Import and exact conditional restoration

`vsr_mesh_import(request_id, scene_id, handle, target_version, dependencies,
file_name)` accepts a confined `.vmesh`, with current equality-only target and
complete dependency evidence copied from Mesh Export. Each dependency has
`payload`, `handle` and `data_version`; payload names are descriptive, not live
reference authority. No prior export artifact or bundle `source` receipt is
required: independently authored replacement bundles use the same current
Scene evidence. Requires current Scene READ+EDIT, target exposure and enabled
agent edits at admission, execution, delivery and immediately before the write.
There is no general reference-editing channel or new permission switch.

The decoder independently checks UTF-8, duplicate/unknown declarations, exact
payload lengths, supported type/component semantics, finite floating samples
and all existing carrier size/count bounds. It additionally caps structural
metadata items at 4096 before constructing the JSON tree (object/array openers,
member colons and separators, excluding punctuation inside strings). This bounds
allocation count as well as serialized header bytes, including descriptive
`source` metadata. Payload offsets follow declaration order, not sorted JSON
object keys; unaligned samples are read without typed-pointer aliasing. Payload
IDs remain opaque bundle-local keys even when their spelling resembles a path;
no source or payload string is resolved as a file or live Object reference.
Decoder cancellation/rejection leaves its prior output intact.

Preflight builds and validates the **whole proposed Geometry**: required enabled
positions, effective indexed/non-indexed triangle count, all stored index ranges,
and vertex/primitive/corner cardinalities, types, finite values and checked byte
lengths. Every disabled attribute still participates. Explicit replacements and
removals can repair incompatible prior data; an incompatible retained field
rejects. Removing indices produces a non-indexed result only if the position
count is divisible by three. Enabling/disabling an optional binding requires a
replacement action; preservation cannot silently change its state. Required
positions cannot be disabled/removed, and empty Arrays/empty meshes are not in
revision 1. No welding, normal generation, normalization, coercion, reordering,
triangulation or geometric-quality repair runs. Equal counts do not certify
correspondence: the agent must deliberately remap/replace/remove affected UVs,
colours and other values. Ephemeral plans contain live pointers for owning-thread
validation only and never enter workers, results or restoration records.

Workers open confined files by held directory identity, reject links/nonprivate
files and quota excess, and decode owned bytes only. The owning thread validates
the entire proposed Geometry and current target/Array evidence again. Unrelated
Geometry, camera and Node work does not invalidate this evidence. An equivalent
replacement is a noncommitting `unchanged` result with current evidence; it does
not create Arrays or restoration history.

Changed payload groups use detached Arrays, not in-place writes to old storage.
No new Scene Array or resolvable handle escapes through preparation or reentrant
host reservation callbacks. All append slots, identities, complete Parameter
maps/presence, count/version bookkeeping, retention, receipt and Activity ownership
are prepared before publication. The final owning-thread guards recheck authority,
deadline/cancellation, target, file, required lifetime/data/mapping, logical pool
indices and the prepared Parameter state. Active ordinary Parameter batches
refuse preparation. Unversioned UI metadata changes cannot be overwritten by a
prepared map swap. Publication of all Arrays and the complete binding-map swap
is nonallocating/nonthrowing, with one target core-version advance and no callbacks.
Only then are ordinary added/parameter-batch/removed notifications and
synchronization delivered: every observer sees the complete final CPU Geometry.
The existing RenderIndex triangle-Geometry batch handler refills the complete
current parameter set and commits ANARI once, including removals; it does not
commit smaller positions against old out-of-range indices. Other object/subtype
batch policies and the delegate/event API are unchanged. All Surface users
keep the same Geometry; other Geometries using any old Array keep its exact bytes
and identity. Name, scalar parameters, subtype, Materials, Images and Nodes are
not editable bundle state.

Poll ordinary `mesh_import` status for `committed`, resulting `target_version`,
`dependencies`, `scene_revision` and normal `batch_ref`. Postcommit notification
or synchronization failure is `applied_with_sync_failure`, **committed=true**,
with eligible recovery. Replay cannot apply twice, and late cancellation cannot
turn publication into rollback. Receipts are historical, not current-frame proof.

Use `vsr_restore` with that normal Batch Reference. Required prior, replacement
and preserved Array identities are pinned under actual Scene INTERNAL ownership,
using relocation-safe lifetime entries. GC cannot collect them while retained;
explicit retirement, any required data write/change-back or target binding change
permanently conflicts. A mapped required Array is busy and remains eligible.
Restore rechecks current authority and every required dependency, then prepares
and atomically recovers prior binding presence, enabled state, identities and
aliases, without rewriting any old storage. Removed old Parameters recover their
prior metadata; still-present Parameters retain unrelated later UI metadata.
Logical-index relocation during a final host guard refuses before publication;
ordinary GC/defragmentation between operations is supported. Restoration is
one-way/no-redo; synchronization failure remains committed.
Replacement Arrays adopted by other users survive. Last-owner release off-thread
only expires a pre-registered owner gate; the owning-thread exchange pump or Scene
GC drains pins and leases. Scene teardown retires Arrays before releasing the
Scene-owned retention leases.

## Supported triangle profile

Required `vertex.position`: FLOAT32_VEC3 or FLOAT64_VEC3, enabled, nonempty.
Optional `primitive.index`: UINT32_VEC3 or UINT64_VEC3; explicit indices must
be in range. Without enabled indices, vertex count must be divisible by three.
Disabled index data is retained with its enabled state, but all stored field
cardinalities must still agree with the effective triangle count.

`vertex`, `primitive` and `faceVarying` prefixes support `normal`, `tangent`,
`color`, `attribute0` through `attribute3`. Associations are respectively
`vertex` (position count), `primitive` (triangle count), `corner` (three times
triangle count). Gathered subset data stays gathered; no foreign material subset
is reinterpreted. Normals have three components, tangents four, colours three
or four; generic attributes one through four. Numeric payloads support FLOAT32,
FLOAT64; signed/unsigned INT8/16/32/64; signed/unsigned FIXED8/16/32, scalar or
VEC2/3/4. ANARI type spelling preserves fixed-point semantics, not just NumPy
storage dtype. All floating values must be finite. Degenerate/open/non-manifold
triangle connectivity is not repaired or rejected for geometric quality.

Arrays must be host-resident, unmapped, one-dimensional and have complete
contiguous bytes and authoritative write tracking. CUDA, proxy, unsupported
layout/type/name/kind, absent bytes or unknown object dependencies refuse the
**whole** export, including disabled unsupported dependencies. Non-Array
ordinary Geometry parameters are not editable bundle fields. Unknown object
references cannot enter the envelope. Initial profile requires little-endian
host storage; no hidden byte conversion or GPU transfer.

Host write tracking is intentionally broader than export eligibility: the SDK
scalar/vector interval `ANARI_INT8` through `ANARI_FLOAT64_VEC4` also includes
FIXED64, UFIXED64 and FLOAT16. Those families receive ordinary write evidence
but are refused by the revision-1 mesh profile. An Array token proves tracked
writes, not a supported bundle representation. Mesh capture validates its
explicit representation/profile before requesting tokens; Image export/import,
inspection and Image tokens still require `isImage()`. Supported mesh families
assert their consecutive scalar/VEC2/3/4 SDK layout and tracking-range inclusion
at compile time. No tracking predicate authorizes a new exchange format.

## Bounds and ownership

Maximum 64 Geometry parameters/fields/unique payloads, 64 KiB JSON header,
64 MiB combined payload; shared directory maximum 1024 files / 256 MiB and
4096 quota-scan entries. Shared exchange transient and physical ceilings also
apply. The maximum carrier size is 67,174,412 bytes (64 MiB payload + 64 KiB header
+ 12-byte prefix); no compression or external members can expand that bound.
The shared transient ceiling defaults to 256 MiB and is clamped to the smaller
configured transient/physical ceiling and 256 MiB. A host's physical reservation
pool may impose a smaller available limit. These reservations cover exchange
ownership, not external NumPy/Trimesh working memory.

A 30-second operation deadline and the shared engine admission pool bound
work/concurrency. Engine defaults are 32 scene-work slots across registered
Scenes, 8 control slots and 1024 status records; hosts may configure other finite
positive limits. Mesh has no additional unbounded queue. Queued, executing,
synchronizing and backend-cleanup work holds a scene-work slot; one admitted
exchange job owns at most one worker. Terminal cancellation/deadline status need
not mean its worker has physically drained. Export creation is serialized, and
physical/transient reservations can refuse work before all slots are used.
Capture runs at the authorized Scene owning-thread safe point; workers receive
owned immutable bytes only. Capture, worker stack/control, result retention and
status copies use physical reservations. Import reserves 288 KiB for request/
canonical/evidence controls before parsing evidence, 2 MiB for bounded decoder
metadata/control in addition to the 512 KiB worker stack and 64 KiB guard, and
actual file bytes before read/decoding. The full resulting receipt is capped at
16 KiB; public status/Activity/restore copies carry their own physical owners.
The decoder's 4096 structural-item bound applies before JSON DOM construction.
All new payload storage has a reservation before detached allocation. Import
bounds the target's copied Parameter metadata to 64 KiB (including Parameter
controls, descriptions, string values/bounds and enumerations) before copying.
The preflight conservatively includes new optional Parameter controls in that
bound so its result remains immediately restorable; oversize proposals refuse
with `mesh_parameter_limit`. Restore also bounds its current target before
preparation. This is a bounded transaction-copy policy,
not a new limit on ordinary Scene editing.

Mesh and Image share the configurable restoration pool (unchanged 16 MiB
default), Activity eligibility and originating-session ceiling (64 MiB).
A mesh batch conservatively charges all unique required prior Array data plus
replacement data, `sizeof(Array) + 4096` bytes per retained Array for lifetime/
control ownership, the snapshot record, and a conservative 256 KiB allowance
for prior Parameter metadata plus expected/proposed maps and added/restored field
controls. Aliases are charged once within one batch; independent batches do not
deduplicate their charges. Existing host Scene
metadata is not a new copied mesh payload, and these limits are not a cap on all
viewer RAM/GPU memory. Image snapshots and mesh lifetime entries additionally
share a physical-retention ledger: Activity eviction or consumed history cannot
make copied snapshots or not-yet-drained pins uncharged. Reservations survive
until the corresponding buffers/pins are physically releasable. Insufficient
mesh capacity reports `required_capacity_bytes` and `configured_capacity_bytes`
before binding publication, with no irreversible fallback or automatic increase.

File creation is descriptor-relative,
exclusive and identity checked; partial files are removed by held identity,
never by following replacement links. Resource/authority/cancellation failures
publish no successful artifact. Changing directory identity disables delivery.

### Working-file lifecycle and failure handling

Mesh Import names are 7–128 characters, a single basename of ASCII letters,
digits, `_`, `-`, `.` ending in `.vmesh`, with no leading dot. Absolute paths,
path separators and traversal paths reject; dots inside a basename do not
resolve a path. Input must be a same-user regular 0600 file with one link in the held
0700 directory. The supplied helper creates private files; files authored by
other tools must meet the same contract. Import does not delete its input.
Completed export files have **no TTL** and survive import, restore, conflict and
undo eviction. Delete unwanted working copies explicitly; deleting them does
not discard eligible undo. Normal service teardown joins workers and cleans the
held launch directory without following links or crossing mounts. A crash is not
normal teardown and is not a promise of automatic orphan removal on restart;
fresh launches never adopt an existing launch directory.

Poll ordinary operation status and inspect `committed` before deciding recovery:

- `invalid_request` reasons such as `file_rejected`, `mesh_file_limit`,
  unsupported profile/decode errors or `mesh_parameter_limit` are precommit
  refusals. Fix the working file/profile, not the surrounding asset graph.
- Stale evidence/conflict requires fresh export evidence and a deliberate retry;
  no force/merge path exists. An ordinary open map is busy, not silently closed.
- `busy` / `resource_limit` can mean transient, physical, file, slot or retention
  pressure. Retention refusals report `required_capacity_bytes` and
  `configured_capacity_bytes`, plus originating-session capacity/availability.
  Exchange metadata reports `effective_global_retention_byte_cap`,
  `available_global_retention_bytes`, `session_snapshot_byte_cap` and authenticated
  `available_session_snapshot_bytes`. Global availability is the smaller of
  logical eligibility and physical retention availability. Raising only the
  global host budget cannot bypass the 64 MiB originating-session ceiling.
- Authority withdrawal, cancellation or timeout before publication leaves the
  Scene unchanged. Cleanup can finish after the terminal receipt. After
  publication, `applied_with_sync_failure` remains **committed=true**; do not
  retry it as if nothing happened. Inspect state and use its normal `batch_ref`
  when still eligible. Historical replay neither repeats mutation nor refreshes
  a file, eligibility or rendered image.

These are error families, not a replacement for the receipt's exact reason and
current public status. No capacity refusal silently increases a budget, drops
undo, saves the Scene or rewrites an original asset.

## Python / NumPy helper

For runnable selective edits, a real Trimesh count-changing workflow,
independent authoring and explicit conversion-loss/correspondence handling, see
[the mesh-tool cookbook](mesh-tool-cookbook.md). Its topology-only adapter is
installed alongside this lossless helper; external tools never run in Import.

The installed `share/vela/python/vsr_mesh.py` requires NumPy; add that directory
to `PYTHONPATH`. No private viewer API or Archive parsing is needed:

```python
from vsr_mesh import Bundle

mesh = Bundle.load(exchange_directory + '/' + receipt['file_name'])
print(mesh.inspect())                    # exact types/actions/associations
positions = mesh.read('vertex.position') # detached typed NumPy copy
positions[:, 2] += 0.1
mesh.write('vertex.position', positions) # exact shape/dtype; updates aliases
mesh.save(exchange_directory + '/edited.vmesh')
```

Then call `vsr_mesh_import` with `file_name: "edited.vmesh"`, the target handle
and the separately retained current `target_version`/`dependencies`. If positions
alias a field that must stay preserved, use `replace('vertex.position', positions,
'ANARI_FLOAT32_VEC3')` instead of `write`: only the declared position binding is
split. Restore using the returned `batch_ref`, not the working file.

`Bundle.create()`, `replace(name, ndarray, element_type, enabled=True)`,
`alias(name, source, enabled=True)` and `remove(name)` construct explicit
replacement intent. `read` never changes intent; `write` refuses dtype/shape
changes; `replace` permits count changes without coercion. Callers must remap,
replace or remove incompatible attributes after topology/correspondence changes.
Equal counts do not prove correspondence. Helper `save` atomically replaces a
local working file and refuses a destination symlink; do not use it on source
assets. Helper files are untrusted input: viewer Import independently enforces
confinement, bounds, field actions, current evidence and coherent final validation.
