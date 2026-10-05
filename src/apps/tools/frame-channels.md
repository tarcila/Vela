# Shared Frame Channel command-line options

The tools and Viewport use `FrameChannelSelection` and the running device's
`FrameChannelCatalog`, not private mode tables or catalog indices. These CLI
spellings are the shared contract for later tool adapters:

| Option | Meaning |
| --- | --- |
| `--list-channels` | List exact presentation names, types and compatible modes; do not render/write an image. |
| `--channel NAME` | Exact, case-sensitive presentation name. Strip only the catalog's single leading `channel.`; unprefixed advertised names remain unchanged. |
| `--visualization MODE` | Explicit compatible shared visualization name. Without it, resolve the channel's semantic/type default. |
| `--range auto` | Use the current completed frame's finite range (default for vsrOffline). |
| `--range fixed --range-min MIN --range-max MAX` | Supply both finite numeric bounds, with MIN strictly less than MAX. Clip values outside the bounds. Order of the options is immaterial. |

Bounds are a pair, never partially filled using internal selection defaults.
Bounds with Auto are rejected rather than silently ignored. Unknown policy,
malformed numbers, nonfinite bounds, missing bounds and unordered bounds fail.

Shared visualization spellings are `color`, `grayscale`, `normal`, `id-colors`,
`edges`, `component-x`, `component-y`, `component-z`, `component-w`, `magnitude`.
Use the catalog for compatibility and defaults. Compatibility in a listing is
not proof of a successful map or of an implemented converter. At this stage
vsrOffline implements ordinary Color, FLOAT32/UINT32 scalar `grayscale`,
FLOAT32 vectors with two through four components, native FIXED16_VEC3, and
normalized-byte linear and sRGB RGB/RGBA. FIXED16_VEC3 occupies six bytes per
pixel: three signed int16 components decoded by clamping raw / 32767 to [-1, 1].
Both -32768 and -32767 decode to -1; zero decodes to 0, and 32767 to +1.
Vectors offer their existing components and Euclidean `magnitude`; RGB/RGBA also offer `color`. An unfamiliar vec2 defaults to
`component-x`, while unfamiliar RGB/RGBA vectors default to `color`. Standard
directions named exactly `normal`, `shadingNormal`, `tangent` or `bitangent`
(with or without one leading `channel.`) default to `normal` when compatible.
These directions and `albedo` prefer advertised FLOAT32 RGB over alternatives;
`albedo` defaults to `color`. Names and case are preserved; substring matches,
aliases and double prefixes do not imply direction semantics. Explicit `normal`
is available on any FLOAT32_VEC3 or FIXED16_VEC3, including unfamiliar channels
whose default remains `color`. Four-component vectors and normalized bytes do
not offer `normal`. Unsupported conversions fail explicitly instead of
substituting Color.

vsrOffline defaults to `color` and keeps its existing importer, camera,
sampling, animation and output-format workflow. For example:

```
vsrOffline --lib DEVICE --channel Temperature_RAW --range auto --campos 0 0 3 -o diagnostic.png
vsrOffline --lib DEVICE --channel Temperature_RAW --visualization grayscale --range fixed --range-min -2 --range-max 8 --campos 0 0 3 -o diagnostic.png
```

The public source/pass status must be VALID before saving each selected frame.
Pending work is not a failed map; no pending or failed diagnostic frame is saved.
Errors identify the request, Device Identifier and renderer and suggest
`--list-channels`. Earlier successful animation frames are retained if a later
frame fails. Image writing retains the existing bottom-up-frame to top-down-file
conversion. Scalar, component, magnitude and normal diagnostics bypass beauty
exposure/tone mapping by using the shared visualization pass; ordinary Color
remains unchanged. Diagnostic Color converts linear RGB to sRGB, preserves
already-sRGB bytes and keeps alpha linear. Normal colors map [-1, 1] to [0, 1]
component-wise without a beauty transform or length normalization, with opaque
alpha. Negative Color components instead clamp to zero before linear-to-sRGB
conversion.

Controlled ANARI fixture tests cover fixed16-only catalogs, actual source copies,
CPU/CUDA visualization and tool output. They do not certify external Barney:
its repository/build is inaccessible, and its advertised types, units and
coordinate spaces remain unverified.

See `src/vsr/rendering/CONTEXT.md` for shared selection, production and scalar
conversion contracts, including the agreed constant, nonfinite and background
pixel defaults. Both tools use these same defaults.
