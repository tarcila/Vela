# Mesh Exchange with NumPy and Trimesh

These examples run **in the agent's external Python process**, not inside the
privileged Mesh Import service. No native modeling verb, asset/material importer,
viewer private API or manual Archive decoding is involved. They address one
existing triangle Geometry through public MCP, in Geometry-local coordinates;
Node transforms are never baked into positions.

## Setup and the public transaction

Requires Python 3, NumPy and Trimesh. This workstation's parent flake declares
`numpy` and `trimesh`; check the actual runtime, not just package declarations:

```sh
python3 -c 'import numpy, trimesh; print(numpy.__version__, trimesh.__version__)'
```

Elsewhere install those packages in your external Python environment. The core
`vsr_mesh` helper only needs NumPy; `vsr_mesh_trimesh` imports Trimesh lazily when
constructing a tool mesh. The adapter is qualified with NumPy 2.4.4 / Trimesh
4.12.2; the public integration and helper tests must pass for other versions.

Source interfaces (from repository root):

```sh
export VSR_MESH_HELPER_PATH="$PWD/python"
export PYTHONPATH="$VSR_MESH_HELPER_PATH${PYTHONPATH:+:$PYTHONPATH}"
```

Installed interfaces (substitute your installation prefix):

```sh
prefix="$PWD/_out"
export VSR_MESH_HELPER_PATH="$prefix/share/vela/python"
export PYTHONPATH="$VSR_MESH_HELPER_PATH${PYTHONPATH:+:$PYTHONPATH}"
# Examples: $prefix/share/vela/examples/mesh-{numpy-edit,tool-replacement}.py
# Docs:     $prefix/share/vela/docs/mesh-tool-cookbook.md
```

1. Use public discovery/inspection to choose the registered Scene and exposed
   Geometry handle. Export with `vsr_mesh_export` and poll ordinary operation
   status. Copy its `target_version` and **complete** `dependencies` as opaque
   equality-only evidence, separately from the working bundle. Export is the
   current public way to obtain this complete evidence; no mesh-specific
   inspection endpoint is assumed here.
2. Get the host-published mesh exchange directory, and process the returned
   working file in your external tool. Never pass an original OBJ/PLY/GLTF asset
   as an output. The scripts refuse an existing output or the input path.
3. Read the conversion/action report. Decide how every affected binding is to
   be preserved, remapped/replaced or explicitly removed. The LLM/user chooses
   operations and correspondence policy, not the import service or adapter.
4. Call public `vsr_mesh_import` with `request_id`, `scene_id`, `handle`, fresh
   `target_version`, `dependencies` and **only the confined filename**, e.g.
   `"edited.vmesh"`. Poll status. `committed` and `outcome` are distinct:
   postcommit synchronization failure is still a committed edit with recovery.
5. Render through the existing device/index to verify actual pixels. A receipt
   or revision tag is not proof of refreshed device buffers. Use public
   `vsr_restore` with `target_batch_ref` set to the returned `batch_ref` for exact
   conditional recovery, then render again. Do not recreate a device as a
   refresh workaround. An isolated independent render is not presented-viewport
   evidence.

The bundle's optional `source` is descriptive, never import authority. An export
receipt is not mandatory *provenance*: a separately authored `Bundle.create()`
needs no source metadata or derivation from an exported file. It still needs
current target/dependency evidence and normal READ+EDIT/exposure/agent-edit
permissions. The example below does not read its input in independent mode.
Do not invent tokens or treat historical replay as fresh evidence. Conflicts
require fresh evidence and a deliberate retry; no force, merge or silent repair.

## Selective NumPy edit with deliberately maintained correspondence

```sh
python3 scripts/mesh-numpy-edit.py \
  "$exchange/exported.vmesh" "$exchange/numpy-edited.vmesh" --offset-x 0.1
```

This translates only positions, preserving their exact original ANARI type and
component width. All other binding bytes, types, enabled states and aliases stay
in the bundle. `replace` deliberately splits just the position binding if it
previously aliased another field; the script reports those aliases. If editing
all aliases is intended, pass `--shared-payload`, which uses `Bundle.write` and
marks the whole payload group `replace`. **Do not** choose that option for a
position/normal alias unless changing the normals too is genuinely intended.

Equivalent API usage:

```python
from vsr_mesh import Bundle

bundle = Bundle.load(input_working_file)
metadata = bundle.inspect()
positions = bundle.read('vertex.position')
positions[:, 0] += 0.1
field = metadata['fields']['vertex.position']
bundle.replace('vertex.position', positions,
               metadata['arrays'][field['payload']]['element_type'])
bundle.save(new_working_file)
```

This does not infer that all equal-count edits preserve semantic correspondence.
Translations do here by deliberate choice; arbitrary reorderings do not.

## Real count-changing tool operation

```sh
python3 scripts/mesh-tool-replacement.py \
  "$exchange/exported.vmesh" "$exchange/tool-edited.vmesh"
```

This topology-only example constructs a real Trimesh from named typed positions
and indices, calls `subdivide()`, then scales locally by 0.4 for a discriminating
silhouette. A triangle becomes six vertices/four triangles. It explicitly removes
old optional attributes, supplies intentionally authored red per-primitive colour
and an explicit disabled alias. This is an example **loss decision**, not an
appearance-preserving remesher. Read the printed `unsupported_fields`,
`discarded_fields`, `replaced_fields`, representation changes and effective
field actions. Adapt the resolution policy to the intended asset appearance.
Existing Materials/Samplers/Images are not imported or changed; removing a UV
binding can change their rendered appearance, which is why it is explicit.

For deliberate remapping rather than dropping attributes:

```python
from vsr_mesh_trimesh import to_trimesh, replace_topology, Replacement, Alias

mesh, transfer = to_trimesh(bundle)
print(transfer)  # all optional fields unsupported by this topology-only path
result = mesh.subdivide()  # chosen externally, NOT run by the host
# The agent supplies these, with exact ndarray dtype and ANARI semantics.
resolutions = {
    'vertex.color': Replacement(remapped_rgba_u8, 'ANARI_UFIXED8_VEC4', False),
    'vertex.attribute0': Alias('vertex.color', False),
    'vertex.normal': 'remove',
}
edited, report = replace_topology(bundle, result, correspondence='lost',
                                  resolutions=resolutions)
print(report)
edited.save(new_working_file)
```

Every other old optional binding also needs an explicit resolution in this
`lost` case, including disabled fields and per-primitive/per-corner fields.
Unknown resolutions, missing mappings, incompatible counts and alias chains/
cycles reject. Direct aliases name an existing non-alias source; no implicit
byte deduplication. `Replacement` delegates exact type/shape writing to the
canonical helper. No UV interpolation, normal invention, fixed-point decoding,
material subset conversion or correspondence inference is performed.

For a real tool translation with deliberately maintained correspondence, run
the same script with `--maintained` (it translates by +10 locally instead of
subdividing). The public tracer checks retained primitive appearance bindings,
UFIXED8 colour bytes/types, UV aliases and disabled state on this path.

With `correspondence='maintained'`, omitted optional fields retain their **exact**
bytes, ANARI representation (e.g. UFIXED8, not merely uint8), enabled state and
alias relationships in the original bundle, not in Trimesh. Their cardinalities
must still match. An old optional alias of positions/indices requires an explicit
resolution even here; preserving it deliberately splits it from replaced topology
and `split_topology_aliases` reports that action. Use an explicit `Alias` instead
if it should follow the changed topology payload. Equal counts never certify
correspondence. The host independently validates the whole proposed Geometry.

## Independently authored replacement

```sh
python3 scripts/mesh-tool-replacement.py \
  "$exchange/never-exported.vmesh" "$exchange/authored.vmesh" --independent
```

This constructs a four-vertex/two-triangle square directly with Trimesh and
`Bundle.create()`. The input need not exist and is not read. No `source` field,
export receipt, live Object reference or foreign asset path is embedded. It
explicitly removes target `vertex.attribute0` for the documented test target,
adds colour and declares its alias. For another target, inspect the existing
bindings and explicitly resolve all incompatible/semantically affected bindings;
omission otherwise preserves them and can make the import reject. Submit the
bundle with **fresh evidence for that target**, obtained independently of how the
replacement was authored.

## Actual STL to a new working bundle (external process)

The installed `mesh-stl-to-bundle.py` example uses the **existing Trimesh STL
reader**, then the existing `Bundle.create()` / `replace_topology()` helpers.
It is not a new native STL parser, a universal converter, or a privileged file
reader. Run it only on the intended external input, producing a new working
file in a host-approved input root when that file will be submitted to Content
Creation:

```sh
python3 "$prefix/share/vela/examples/mesh-stl-to-bundle.py" \
  /path/to/selected-part.stl /path/to/private-input/part.vmesh
# If the printed/refused FLOAT64 -> FLOAT32 rounding is acceptable, retry:
python3 "$prefix/share/vela/examples/mesh-stl-to-bundle.py" \
  /path/to/selected-part.stl /path/to/private-input/part.vmesh \
  --allow-precision-loss
```

The external reader accepts binary STL or a single nonempty ASCII-solid result.
Multiple nonempty ASCII solids are deliberately refused, not concatenated or
silently reduced to one; Trimesh may ignore empty solids.
The output has enabled FLOAT32_VEC3 positions and UINT32_VEC3 explicit triangle
indices, with no live Scene/source authority or Material description. Coordinates,
facet order and units are retained; there is no welding, fitting, recentering,
normalization, repair or generated-normal binding. Repeated corners remain separate
vertices, including coincident/degenerate/duplicate facets. The existing Trimesh
reader determines STL parsing semantics; this example does not certify strict
STL grammar or preservation of nonstandard extensions.

Read the JSON report: it records the actual Trimesh version, SHA-256 of the
consumed source bytes, triangle count, exact output field actions, representation
changes and all discarded tool attributes. **STL facet normals, header/solid labels
and attribute-byte/color extensions are omitted**, not claimed as preserved.
There is no source appearance/Material conversion. FLOAT64-to-FLOAT32 changes
are refused unless explicitly acknowledged; overflow/nonfinite positions, empty
geometry, invalid topology and unsupported multi-solid output always fail without
publishing a bundle. Binary FLOAT32 coordinates widen exactly inside Trimesh and
return exactly to FLOAT32 in the qualified profile.

The input is opened read-only and bounded to 64 MiB. Helper output payload/header
bounds still apply, but **Trimesh's external working allocations are not bounded
by the privileged host's processing budget**. No Scene is accessed or changed.
The completed bundle is published exclusively to a new `.vmesh` file (including
against a competing output creator), never replacing an existing file or link.
Original assets are not written. The report stays separate from the bundle;
retain it to judge the deliberate topology-only conversion losses.

**Ticket-02 WIP boundary:** the external conversion is available; public additive
Surface creation is not yet implemented or advertised. This command does not
create a Surface, and must not be described as the complete creation → render →
Restore workflow. Existing `vsr_mesh_import` can use this bundle only to replace
an existing Geometry with independently obtained current target/dependency
evidence and deliberately resolved old bindings. Do not invent a creation tool
or use export metadata as authority to mutate a former target.

Reproduce this helper control (hand-authored binary STL, ASCII precision decision,
invalid/multi-solid rejection and unchanged original files):

```sh
VSR_MESH_HELPER_PATH="$prefix/share/vela/python" \
VSR_MESH_EXAMPLE_PATH="$prefix/share/vela/examples" \
  python3 scripts/test-mesh-stl.py
```

## Conversion contract and explicit errors

- `to_trimesh` transfers only finite FLOAT32/FLOAT64 Nx3 positions and enabled
  UINT32/UINT64 Mx3 triangle indices. Non-indexed data must have a vertex count
  divisible by three and generates explicit sequential indices. Disabled stored
  indices are not transferred; that semantic change is reported.
- Trimesh stores positions as float64 and indices as int64. Exact widening and
  unsigned-to-signed changes are reported and index range checked. Construction
  uses `process=False, validate=False`: no welding, reordering, normal generation
  request, triangulation, normalization or repair by the adapter.
- `replace_topology` outputs enabled explicit indices; it uses the original
  topology types when available, otherwise FLOAT32_VEC3/UINT32_VEC3. Explicit
  `position_type`/`index_type` may choose the supported FLOAT64/UINT64 alternatives.
  Narrowing with changed values raises a precision-loss error unless
  `allow_precision_loss=True` is explicitly selected. The report then records
  `precision_loss: true`. Overflow/nonfinite output still rejects. The example
  explicitly acknowledges scaling-induced float32 rounding; no retained
  attribute is numerically converted.
- All bundle attributes outside topology are reported unsupported **in the
  tool**, not silently discarded from the lossless bundle. Actual removals,
  replacements, retained fields and topology alias splits are separately listed.
  `field_changes` records each attribute's before/after ANARI type, count,
  association, enabled state and effective action, including semantic changes.
  Tool visuals, custom vertex/face attributes and metadata never silently become
  VSR state: they require `discard_tool_attributes=True` and are listed as
  discarded. Tool normals/default colours are always listed as ignored semantics;
  explicitly supply `Replacement` if they are wanted. This is not a universal
  OBJ/PLY/GLTF converter or foreign appearance importer.
- Open/non-manifold/degenerate triangles are not repaired. Arrays must be nonempty
  and respect revision-1 helper/profile limits (64 fields/payloads, 64 MiB payload,
  64 KiB header). Tool algorithms allocate externally: these limits do **not** cap
  Trimesh working RAM. The host has its separate enforced confinement/ownership/
  restoration budgets; see [mesh-exchange.md](mesh-exchange.md).
- Helper errors raise `ValueError`; external algorithm/import failures propagate.
  Candidate generation never mutates the input Bundle. No output is saved until
  a candidate is complete. The host still independently rejects malformed fields,
  incompatible retained attributes, stale/mapped/proxy/CUDA dependencies and
  authority/capacity failures before any Scene write. Postcommit synchronization
  failure must be handled as committed, not as a nonmutating rejection.
- No operation saves the Scene or writes original assets. Geometry/Surface
  identity, Material/Image bindings, name, scalar parameters and Node transforms
  remain outside the mesh editing surface. Restore is exact but conditional,
  one-way and dependent on unchanged required target/old/new Arrays. Working
  files and restoration eligibility have independent lifecycles.

## Reproducible qualification

```sh
VSR_MESH_HELPER_PATH="$PWD/python" python3 scripts/test-mesh-trimesh.py
TMPDIR=/tmp VSR_MESH_HELPER_PATH="$PWD/_out/share/vela/python" \
  _out/bin/vsrIntegrationTests '[mesh-tool]'
```

The public tracer uses isolated Scenes, public MCP export/import/restore, the
installed helper and actual Helide pixels on one device/RenderIndex throughout
replacement and restore. It includes shared target users, old Array isolation,
attribute-driven appearance and independently authored data whose input path
never existed. Source-assets safety and exact type/alias retention are also
helper-tested. VisRTX/Barney and presented-frame qualification are separate
required qualification gates, not claimed by this Helide control.
