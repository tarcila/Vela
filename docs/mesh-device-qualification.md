# Mesh Exchange real-device qualification

This is a private, isolated qualification fixture, not a way to edit a running
viewer or a generic modeling algorithm. The public seam is loopback MCP Mesh
Export → installed helper + external NumPy/Trimesh process → MCP Mesh Import →
MCP Render/Render Image → ordinary MCP Restore. Bulk mesh bytes never travel in
MCP text. Render requests use current public discovery/inspection tokens.

## Run

Build/install with integration, MCP, tests, CUDA and networking enabled. NumPy
and Trimesh must be available to the Python interpreter selected by CMake. Use
one explicit, reviewed ANARI plugin search path; check the actual loaded paths
printed from `/proc/self/maps`. Do not accidentally qualify stale development
plugins ahead of the intended installed plugins in `LD_LIBRARY_PATH`.

From the built workspace, with its own `_out/` (rig layout):

```sh
mkdir -p _out/mesh-qualification-pixels
chmod 700 _out/mesh-qualification-pixels
export VSR_MESH_HELPER_PATH="$PWD/_out/share/vela/python"
export VSR_MESH_EXAMPLE_PATH="$PWD/_out/share/vela/examples"
export VSR_MESH_QUALIFICATION_EVIDENCE="$PWD/_out/mesh-qualification-pixels"
# Set LD_LIBRARY_PATH explicitly to this workspace's install, reviewed ANARI
# plugin directories and required driver/runtime directories on this machine.
for library in helide visrtx barney; do
  VSR_MESH_QUALIFICATION_LIBRARY="$library" \
    _out/bin/vsrIntegrationTests '[.mesh-device]' \
    > "_out/mesh-qualification-$library.log" 2>&1 || exit 1
done
ctest --test-dir _out/_cmake -C RelWithDebInfo --output-on-failure
```

The CTest fast control `vsr::mesh-device-helide` uses real Helide, and
`vsr::mesh-device-edit-helper` tests the real external CLI. The GPU scenario has
an explicit opt-in tag rather than joining the portable `[integration]` suite.
**Both `default@visrtx` and `default@barney` must pass the explicit command** to
qualify required GPU coverage; a passing portable suite/Helide run is not that
claim. Missing/unknown qualification selectors and missing required libraries
fail, never skip or silently fall back. Source-tree execution is also supported;
without installed-path overrides the existing source helper/example defaults
apply. Installed acceptance must supply the overrides and check matching
source/install hashes.

## What the fixture proves

Three separated regions show two Surfaces sharing the target Geometry and an
unrelated Geometry sharing all the target's original Arrays. The first target
Surface samples a two-colour Image through UVs; the second uses per-primitive
red/green colour. The third uses the original shared Geometry and colour.
Materials, Sampler/Image, Surface identities, three placement transforms, the
private camera, Geometry name/custom parameter, original Array bytes/types and
binding identities remain invariant. Assertions run before every real render,
including while replacements are bound, not only after restoration.

The original indexed square has four vertices/two triangles. Each round exports
fresh evidence and runs the **installed** `mesh-device-edit.py` outside the host:

1. Real Trimesh subdivision: nine vertices/eight triangles, scaled by 3/8.
2. Maintained correspondence: five vertices/two triangles, scaled by 1/2. The
   added vertex is intentionally unused; existing primitives keep their exact
   live colour/disabled-alias bindings. This is a vertex-cardinality refresh,
   not a claim that the extra vertex contributes to the silhouette.
3. Real Trimesh subdivision again: nine vertices/eight triangles, scaled by 5/8.

All three visibly shrink both target silhouettes. The square-specific external
policy explicitly remaps UVs, supplies planar normals and maps subdivision
children to exact original primitive colours. Disabled colour alias intent is
maintained. It reports tool representation/attribute limitations and all
replacement/retention actions. The maintained round retains exact primitive
payloads: export's explicit replacement actions remain in the bundle, and the
normal Import equivalence rule retains their existing live identities. No
manual envelope rewriting or new helper API is needed. The CLI refuses a
non-square input or an existing output; tests verify the input file stays exact.
This is not a universal remeshing/correspondence inference service.

The same ANARI device, renderer, RenderIndexAllLayers, world and camera remain
bound throughout. `populate()` occurs only once before the baseline. Imports
and restores use normal Scene notifications. Fresh per-request Frame objects
avoid progressive accumulation contaminating comparisons; they do **not**
recreate the device, world, Geometry or index as a refresh workaround.

At 192×96 with fixed camera, black background, directional light and eight
pixel samples, each 64×96 region must initially contain >80 pixels of each
expected dominant colour. Each changed target region must retain >10 pixels
of each colour but have <half its original coloured silhouette. It must also
fail the restoration/unchanged comparison. This distinguishes native resource
refresh from matching revision tags, initial upload or an unchanged placeholder.
A repeat baseline tests stability in all three regions.

For unchanged/restored regions, a pixel differs when any RGB channel differs by
>12/255; at most 61/6144 pixels (<1%) may differ. This accommodates stochastic
edge sampling, not broad appearance changes. Exact CPU bytes/bindings are
asserted separately. Logs report actual colour counts and whether recovered
pixels were **exactly** equal (stronger than the tolerance). Optional evidence
files are the actual public Render Image PNGs, labeled `independent-*`.

These are **independent renders, not presented-viewport frames**. No viewer
presentation, upload qualification, user camera/configuration, chess Scene or
live descriptor is involved. Record plugin/frontend paths and hashes, GPU and
driver, build/cache options, installed binary/helper hashes, commands/results
and any library warnings alongside local evidence. A device failure is a
blocker on its separate backend track, not authorization to patch packages,
replace a library silently, recreate/switch devices or waive required coverage.
