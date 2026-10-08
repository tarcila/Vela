# Agent Mesh Export and typed working bundles

`vsr_mesh_export(request_id, scene_id, handle)` exports one currently exposed
triangle Geometry with Scene READ (EDIT is not required). Poll ordinary
operation status for kind `mesh_export`, `file_name`, `target_version` and
`dependencies`. The descriptor's Image exchange metadata additionally publishes
`mesh_export`, `mesh_import: false`, `mesh_exchange_directory` and finite mesh
limits. Both formats share the same launch-private directory and quotas. Mesh
Import is **not implemented or advertised** by this slice.

The receipt is historical: replay does not rewrite files or certify their
current contents. Export never maps, changes Arrays/bindings, advances versions,
or allocates restoration retention. Exported files remain working copies until
explicit removal or launch cleanup. They are not an undo store. Preserve the
freshness evidence separately when processing independently authored bundles.
Evidence tokens are equality-only and scoped to Geometry identity/parameter
state and the bound Arrays' identity/lifetime/data state, not a global revision.

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
split it deliberately. Untouched helper processing preserves bytes, types,
actions and alias intent. These action rules are the envelope contract for the
future mutating slice, not a currently available import endpoint.

Unknown revisions, unsupported fields/types, ambiguous declarations and
malformed lengths are refused; no implicit coercion or geometry repair.

### Import preflight implementation (not a public Import)

The internal C++ decoder and positions preflight are present, but remain
**unconnected to MCP and Scene mutation**. Import is still unavailable. This
preflight is not restoration retention, a binding transaction or permission to
publish a write.

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

Positions preflight accepts only explicit, enabled-state-preserving,
count-preserving position replacement, with omitted/preserved fields retaining
their current bindings. Other replacements, removals, binding-state changes and
count changes reject explicitly. It validates the complete proposed triangle
Geometry, including retained/disabled attributes and index ranges. Replacing
positions may repair nonfinite old position samples, but cannot repair an old
Array still used by a preserved alias. Byte/type-equivalent replacements are
identified without inventing a different binding graph. The ephemeral plan
contains live pointers for owning-thread validation only and must not enter
workers, results or restoration records.

The remaining Import implementation must supply confined I/O, current authority
and freshness checks, charged Scene lifetime retention, staged reservations and
the atomic internal binding transaction, normal shared restoration and the
public rendered recovery tracer before advertising Import.

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
apply. A 30-second deadline and normal engine scene slots bound work/concurrency.
Capture runs at the authorized Scene owning-thread safe point; workers receive
owned immutable bytes only. Capture, worker stack/control, result retention and
status copies use physical reservations. File creation is descriptor-relative,
exclusive and identity checked; partial files are removed by held identity,
never by following replacement links. Resource/authority/cancellation failures
publish no successful artifact. Changing directory identity disables delivery.

## Python / NumPy helper

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

`Bundle.create()`, `replace(name, ndarray, element_type, enabled=True)`,
`alias(name, source, enabled=True)` and `remove(name)` construct explicit
replacement intent. `read` never changes intent; `write` refuses dtype/shape
changes; `replace` permits count changes without coercion. Callers must remap,
replace or remove incompatible attributes after topology/correspondence changes.
Equal counts do not prove correspondence. Helper `save` atomically replaces a
local working file and refuses a destination symlink; do not use it on source
assets. Helper files are untrusted input for any future viewer import, which
must independently enforce confinement, bounds and coherent final validation.
