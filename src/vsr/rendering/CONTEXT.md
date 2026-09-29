# VSR Rendering

Turns VSR scenes into images: render indexes feed ANARI devices, and an image
pipeline of composable passes produces the final per-pixel output.

## Language

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
