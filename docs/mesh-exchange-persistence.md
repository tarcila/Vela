# Persist an imported mesh as ordinary Scene data

[Mesh Exchange](mesh-exchange.md) replaces supported Array bindings on the
existing Geometry. A successful, committed `vsr_mesh_import` owns its new
host-resident Array bytes in the Scene; the bundle is neither an asset reference
nor a lazy data source. All Surfaces using that Geometry see the replacement.
An unrelated Geometry using its prior Arrays keeps its prior data.

After checking the import's committed/outcome and synchronization fields, the
host/application can explicitly save the Scene through the normal native API:

```cpp
#include "vsr/io/archives/SceneArchive.hpp"

bool saved = vsr::io::save_SceneArchive(scene, "edited.vsr");
// In a different Scene, without the exchange bundle or exporting launch:
bool loaded = vsr::io::load_SceneArchive(reconstructedScene, "edited.vsr");
```

The default `ArrayDataPolicy::IncludeData` is required for self-contained
persistence. `ProxyOnly` deliberately omits payloads and is **not** a lossless
mesh save. The existing Scene Archive stores exact ANARI element types, raw
Array bytes, dimensions, parameter presence/enabled state and object-reference
sharing. Parameter names retain vertex/primitive/corner associations. Declared
Array aliases reload as aliases; independent, byte-identical Arrays do not
become aliases. Materials, Samplers, Images, Layers and Nodes remain ordinary
Scene state, including their sharing and transforms. Archive reconstruction
renumbers pooled objects/nodes; old process-local handles are not persistent
identifiers.

No Scene Archive schema, core Data Tree encoding or general Object Archive
loading semantics were changed for Mesh Exchange. The Scene Archive is not a
Mesh Import envelope: load replaces the target Scene, while Mesh Import edits
one existing Geometry's permitted bindings.

## Separate data persistence from launch authority

- Import does not save a Scene or rewrite original OBJ/GLTF/texture assets.
  Saving remains an explicit application/user action and does not rewrite those
  foreign-format assets either.
- Completed exchange files can be deleted **before saving**, as well as before
  loading/rendering. No exchange path is needed to reconstruct imported data.
- A Scene Archive does not carry the exporting launch's sessions, permissions,
  equality-only freshness evidence, replay receipts or restoration history.
  A prior `batch_ref` cannot become usable undo authority by loading the Scene.
  In a new host, discover fresh handles and export fresh evidence before editing.
- Saving does not consume or extend an existing launch's conditional restore
  eligibility. File cleanup and launch-local recovery have independent lifetimes.

## Reproducible regression

`src/tests/integration/test_IntegrationMeshPersistence.cpp` runs an isolated real
loopback MCP host. Public inspection/export supplies the handle and freshness
evidence; `scripts/test-mesh-persistence.py` uses the documented installed NumPy
helper to make indexed and non-indexed count-changing replacements. The result
includes enabled vertex normals/UVs and per-primitive colours, deliberate aliases,
independent equal-byte Arrays, optional removal, disabled FLOAT64 tangents,
UFIXED8 colours, UINT64 values above exact double precision and gathered INT16
corner data. Disabled values are still checked byte-for-byte, not coerced into a
renderer-friendly representation.

The test renders textured and attribute-driven shared users with `default@helide`
on the bound original device/index, deletes **all** working files, explicitly
saves a normal full Scene Archive and destroys the exporting host. A fresh host
loads that file through `load_SceneArchive`, checks exact typed bytes/bindings and
surrounding state, and actually renders it. Loaded pixels must match the imported
result in both placements, not merely carry matching revision tags. Old handles
and restoration references are refused. Original asset sentinel files and an
explicitly saved pre-import Archive remain byte-identical; import creates no
automatic edited Archive. These are isolated independent renders, not evidence
of presented-viewport or VisRTX/Barney qualification.

From a configured workspace, build/install first and use that installed helper:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=3 just build
TMPDIR=/tmp VSR_MESH_HELPER_PATH="$PWD/_out/share/vela/python" \
  _out/bin/vsrIntegrationTests '[mesh-persistence]'
```

The source joins the existing integration test target automatically; the full
`vsr::integration` CTest suite runs it alongside existing Image, parameter/Node,
inspection, rendering and restoration regressions.
