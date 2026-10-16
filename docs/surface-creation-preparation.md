# Private Surface graph preparation (incomplete)

There is **no public Surface creation operation or capability yet**. This note
records the internal `content::detail::prepareSurfaceGraph()` boundary for the
ongoing compound-creation implementation, not an alternative privileged API.
Do not copy its private Scene into a live Scene using ordinary constructors.
The unregistered `PreparedSurfacePublication` / `PreparedSurfaceRemoval` seams
now provide owning-thread structural adoption and removal; they are not a public
request, async worker, or ordinary shared Restore adapter.

## Prepared output

An owning, nonmovable `PreparedSurfaceGraph` contains a separate Scene with no
external delegates or borrowed objects: exactly one triangle Geometry, one
opaque matte Material, one Surface, one named transform Content Root and its
Surface placement, and one Array per referenced bundle payload. Ordinary Scene
camera/default-Material objects are removed from this private Scene before
construction. Destruction abandons the entire preparation, with no live Scene
input, references or publication. Bundle source metadata cannot select a target.
Two preparations never deduplicate objects by name, filename or payload bytes.

Input is the existing bounded little-endian revision-1 `.vmesh` carrier (64 KiB
header, 64 MiB payload, 64 fields/Arrays). The decoder and complete proposed-mesh
validator are reused. Indexed/nonindexed triangle counts, index ranges, finite
values, full attribute cardinalities/types, enabled states and aliases must be
valid; this is not a repair or triangulation step. Copies use each Array's full
byte length, including 64-bit and fixed-point values. Repeated payload IDs share
one Array. Distinct IDs stay distinct even if every byte is equal.

For **new** Geometry only:

- `preserve` (including ordinary export output) and `replace` both supply new
  Array data. There is no prior live field to preserve or authority to borrow.
- Omitted fields are absent; `remove` on an optional field also means absent.
  Positions remain mandatory and enabled. No fields are inherited from an old
  export target.
- An unreferenced declared payload rejects with `unreferenced_mesh_payload`;
  it is not silently dropped or called lossless creation.
- The owned Material is deliberately set to matte color `(0.8, 0.8, 0.8)`,
  opacity `1`, alpha mode `opaque` (including its UI selection). The private
  settings allow a finite `[0,1]` RGB matte color and a finite affine initial
  root transform. Constructor red/blend defaults are not used as appearance.
- No borrowed Material choice is admitted by this internal slice. A future
  public request must separately support typed new appearance and explicit
  borrowed identity/lifetime/current-resource evidence, without value mutation.

The input carrier is consumed. Validation/error/cancellation leaves a previous
output owner unchanged; allocation exceptions propagate with that same ownership
property. Cooperative abandonment is checked during decoding, before private
construction, between Array copies and after complete validation. It is not a
promise of interrupting a single allocation, copy or structural scan immediately.

## Unregistered structural publication/removal

`PreparedSurfacePublication` profiles guarded libstdc++ pool/Forest/registry
backing and metadata, reserves processing ownership before detached construction,
remaps each distinct Array once, prepares stable typed identities and a bounded
inventory, and enrolls the complete graph before post-write callbacks. Indexed
and nonindexed fields retain complete bytes, types, enabled flags and aliases.
It can target an existing parent or explicitly own a new Layer. Commit, evidence
sealing, notifications and first identity lookups require no further allocation.
Abandoned preparation can leave pool/registry backing capacity, not logical
content or exposed new identities. The processing reservation is a conservative
peak bound, not a claim of physical reclamation or a permanent live-memory grant.

`SurfaceRemovalEvidence` is fixed metadata only: it owns no Scene, Layer or Array
payload. It checks owned parameter/metadata/data-write histories, exact bindings,
node membership and local child-list history, as well as alias-aware use counts
and a bounded scan of ordinary/raw/default/min/max/instance/object-Array uses.
`ForestNode::childrenVersion()` supplies scoped add/remove change-back history
without invalidating the closure for unrelated sibling or Layer edits. Relocation
preserves this token. The owning-thread removal plan reserves replacements,
free-list and tombstone backing before writes, unregisters objects and both node
lifetime entries before callbacks, then notifies once despite consumer failure.
Its processing reservation covers the temporarily transferred tombstone payload;
this is not a full-payload undo copy. Destroy publication/removal plans promptly
after notification; retaining plans also retains their charged processing owners
(and, for an owned Layer or removal tombstones, actual resources).

These structural seams do **not** supply shared-history enrollment, copied-result
or weak-control accounting, public synchronization-hook ownership, async/source/
authority checks, or borrowed Material handling. They must remain unregistered
until all of those gates are connected and verified. Source preparation remains
separately owned/charged by its eventual worker. A callback exception is committed
structural state, not rollback; no public recovery receipt is promised by these
internal primitives. The public adapter must prepare normal recovery and exactly-
once creation/Restore failure-hook owners before allowing writes.

Public MCP/status/inspection/render/Restore, STL into that public path, meaningful
VisRTX placement/removal, resident IncludeData persistence and independent delivery
qualification remain required under the same Surface-creation ticket. The private
preparation tests and ordinary-allocation sweep supplement those gates; they do
not satisfy them or authorize live Scene access.
