# VSR Rendering

Turns VSR scenes into images: render indexes feed ANARI devices, and an image
pipeline of composable passes produces the final per-pixel output.

## Named-channel production contract

`FrameChannelSelection` is shared name-based state: full `deviceName`, resolved
internal `pixelType`, visualization, `ChannelRangePolicy::AUTO` / `FIXED`,
`rangeMin` / `rangeMax` and `invertEdges`. Catalog resolution accepts only exact
presentation names, reports ambiguity/unavailable types, and validates finite
ordered Fixed bounds. Common visualization spellings are `color`, `grayscale`,
`normal`, `id-colors`, `edges`, `component-x`, `component-y`, `component-z`,
`component-w`, `magnitude`. Common range spellings are `auto` and `fixed`;
tool adapters use `--range auto` or `--range fixed --range-min N --range-max N`.
Compatibility is not a promise that every conversion is already implemented.

Image Passes declare `requiredNamedChannels()` as full-name/type requests.
Enabled-pass requests compose with standard bit demands; discovery alone does
not request, allocate or map channel data. The ANARI Image Source validates
storage against its catalog and preserves standard consumers' storage choices.
Beauty Color uses the source's configured format (including HDR) when advertised,
otherwise an advertised supported Color representation. Diagnostic named Color
negotiates its requested representation with the producer without overwriting
that configured beauty preference; returning to beauty restores it. Conflicting
named storage requests still fail explicitly. Conversion and finite-range
reduction live in mirrored CPU/CUDA free functions under `vsr/algorithms/`;
the Image Pass selects the visualization and dispatches the backend.

`ImagePipeline::channelResult(fullName)` and `ImageBuffers::namedChannels`
expose source-owned read-only views. Absent demand has no result. `PENDING`
means no completed frame of this selection/size is available; only `VALID`
permits reading `data`. `FAILED` clears data and supplies the complete name,
error and public ANARI device/renderer handles; adapters already owning their
Device Identifier and renderer name can construct contextual diagnostics.
A failed selection can be reconfigured or refreshed with the renderer; it is
not silently mapped again as Color. Views expire at the next render or any
size/demand/source change. While same-selection work accumulates asynchronously,
the last valid completed frame remains readable. A transition clears old views.

Named maps always use the advertised full name, including unprefixed names.
Host maps are copied before unmapping into source-owned CPU storage or CUDA
managed storage, following the configured pipeline allocation route; no guessed
`CUDA` suffix is appended to arbitrary names. Ordinary standard-channel CUDA
maps, Color/HDR transforms, FrameState exchange and isolated picking are retained.

## Direction-vector storage and interpretation

Native FIXED16_VEC3 is packed six-byte RGB with three signed-normalized int16
components. Shared layout and decoding drive catalog compatibility, source copy
allocation and CPU/CUDA conversion. Clamp each raw / 32767 to [-1, 1]: -32768
and -32767 become -1, zero remains zero, and 32767 becomes +1. This does not
add other fixed16 shapes or change existing float/normalized-byte storage.

The `normal` view accepts FLOAT32_VEC3 and FIXED16_VEC3 regardless of name.
It maps each signed component into RGB with (v + 1) / 2, clamps display values,
and writes opaque alpha, without gamma or vector-length normalization.
Components, magnitude and Color are also available for native fixed16 RGB.
Exact `normal`, `shadingNormal`, `tangent` and `bitangent` names (optionally
one leading `channel.`) default to Normal and prefer an advertised FLOAT32_VEC3
alternative, as does albedo for Color. Unfamiliar vec3 names default to Color;
case, complete names and one-prefix-only behavior are preserved.

Controlled fixture coverage is not certification of inaccessible external
Barney builds or their types, units and coordinate spaces.

## Scalar Channel Visualization contract

`ChannelVisualizationPass` implements scalar grayscale, vector components and
magnitude, compatible Color/normal conversion and identity colors/edges;
ordinary `channel.color` / `color` leaves the beauty transform path intact. Place it after beauty exposure,
tone mapping and output transformation, before overlays. It does not modify
FrameState exposure. Unsupported conversions are rejected.
Use shared catalog resolution first to validate availability and compatibility.
Only enabled, valid diagnostic selections request named data; standard overlay
requests still compose independently.

Auto range uses the current completed frame's finite minimum and maximum.
Negative samples participate normally. A finite constant Auto frame is opaque
mid-gray (128); nonfinite samples and frames with no finite samples are opaque
black. Fixed requires finite minimum < maximum, clamps values, and maps the
bounds to black/white. Numeric mapping uses double precision (including UINT32
limits) and nearest-byte rounding. Scalar data has no inferred background
sentinel: all finite values participate. Pending transitions may
retain a completed display without claiming new-channel success; invalid and
failed selections clear output to opaque black. Only VALID pass status means
successful visualization, never a fallback
to beauty Color. Public source channel results remain independently available.
`setUseCUDA(false)` exercises host conversion in a CUDA-configured pipeline;
the default uses the pipeline's CUDA stream when present.

## Language

**Frame Channel**:
Per-pixel data produced by a rendering device for a frame, such as color,
depth, normals, or object identity. An image derived from that data, such as
an edge visualization, is not itself a Frame Channel.
_Avoid_: AOV (when referring to a Frame Channel)

**Channel Visualization**:
A visible image derived from a Frame Channel's values, such as grayscale
depth, normal colors, hashed identity colors, or edges. A Frame Channel can
have several Channel Visualizations.
_Avoid_: AOV (when referring to a Channel Visualization)

**Image Pipeline**:
One Image Source, followed by ordered Image Passes, followed by Image Sinks,
all sharing one set of per-pixel buffers for the frame being displayed.

**Image Source**:
The single stage of an Image Pipeline that produces the frame's pixels, such as
an ANARI scene render. Everything after it refines or consumes those pixels.
_Avoid_: first pass, stage 0

**Image Pass**:
An independently enable-able stage that reads and modifies the pixels produced
by the Image Source (tone mapping, outlines, overlays).
_Avoid_: post pass (as a separate term)

**Image Sink**:
A terminal stage that hands the finished pixels to a consumer outside the
pipeline, such as a display texture or a network stream.

**Pick Request**:
A one-off query for what lies under a pixel (depth, object, instance). It is
not part of an Image Pipeline and never alters the displayed image.
_Avoid_: pick pass

**World Bounds Overlay**:
A viewport feature that draws the Box Outline of the rendered world's bounds.
_Avoid_: bounding box pass, scene bounding box rendering

**Outline**:
An image-space silhouette border around an object or primitive, derived from
the rendered ID buffers. An Outline traces what was rendered; it does not
project geometry.
_Avoid_: highlight, selection border

**Box Outline**:
The wireframe formed by the twelve edges of an axis-aligned box, projected
through a camera view. A Box Outline is a generic box drawing; it is not
inherently a bounding box — bounding is one client's interpretation.
_Avoid_: bounding box pass, box wireframe

**Device Identifier**:
The string naming which ANARI device to create, written
`[Device Subtype@]ANARI Library`; an omitted subtype means `default`, and
`default@X` identifies the same device as `X`.
_Avoid_: library name, device name, device spec

**ANARI Library**:
The loadable ANARI implementation named by the part of a Device Identifier
after `@`; one library can provide several Device Subtypes.
_Avoid_: backend, device (when meaning the library)

**Device Subtype**:
The name of one device an ANARI Library provides, given by the part of a
Device Identifier before `@`.
_Avoid_: device type, device flavor
