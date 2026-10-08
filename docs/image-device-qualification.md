# Image Exchange real-device qualification

This isolated qualification uses loopback MCP Image Import → Render/Render Image
→ ordinary Restore. It does not connect to a viewer. These are **independent
renders, never presented-viewport observations**. Mesh qualification does not
prove Image texture refresh: run both fixtures separately.

Build and install the integration/MCP-enabled project first. Set `LD_LIBRARY_PATH`
to the tested install and the reviewed plugin/driver directories. With the rig:

```sh
mkdir -p _out/image-qualification-pixels
chmod 700 _out/image-qualification-pixels
export VSR_IMAGE_QUALIFICATION_EVIDENCE="$PWD/_out/image-qualification-pixels"
for library in helide visrtx barney; do
  VSR_IMAGE_QUALIFICATION_LIBRARY="$library" \
    _out/bin/vsrIntegrationTests '[.image-device]' \
    > "_out/image-qualification-$library.log" 2>&1
  # Preserve every exit code; a deferred failure is still a failure, not a pass.
done
```

`vsr::image-device-helide` is the CTest fast control. The GPU tests are opt-in,
not skipped successes in the portable `[integration]` suite. Missing/unknown
selectors and unavailable libraries fail. Record every command's exit status;
a shell loop's final exit alone is not a verdict for all three devices.

One initial `RenderIndexAllLayers::populate()` uploads the scene. One device,
index, world, camera and renderer stay bound through import and restore. Normal
Scene notifications map/unmap the existing native Image Array; the fixture does
not replace the Image or change its Sampler filter/bindings, repopulate the
index, or switch devices. Per-request Frames avoid stale accumulation, not
stale textures. Two square Surfaces share one 1×1 RGBA8 sRGB Image/Sampler; a
third square is an unrelated green control. A directional light and fixed
camera show all three at 192×96 with eight pixel samples.

Baseline must contain >160 dominant green pixels per 64×96 region. Public
Import replaces green with red: both shared users must then contain >160 red
pixels, <10 green pixels and >160 changed pixels. The control must stay within
61 changed pixels (1% of its region), counting a pixel only if any RGB channel
changes by >12/255. CPU bytes, Image/Sampler/material bindings, device/index/world
and camera are checked independently. Restore must commit, recover exact green
CPU bytes, and return all regions within the same pixel tolerance. Actual PNGs
are written as `independent-image-<library>-baseline|imported|restored.png`.

Pixel checks are deliberately nonfatal after Import so a stale GPU texture does
not prevent attempted ordinary Restore or evidence of the returned committed
state. An exactly matching restored baseline **does not qualify** a backend
whose imported red image stayed green. Capture `/proc/self/maps` plugin paths,
binary/library/helper hashes, source revision, GPU/driver, effective options and
all logs beside the PNGs. Receipt revisions alone cannot prove native refresh.

## Qualification boundary and known limitation

The currently unfixed native Barney texture-refresh path can commit correct CPU
Image bytes but render a stale texture. Actual rendered Image update/restore
qualification on Barney is explicitly deferred until that backend defect is fixed.
Keep this fixture, assertions and failing logs/PNGs unchanged: deferral is not a
pass. Shared CPU mutation/recovery, authority, retention/approval and persistence
checks remain required, as do actual Mesh checks on Barney, VisRTX and Helide.

Actual Image update/restore rendering remains required on Helide and a patched
VisRTX library. An older unpatched VisRTX plugin's failure cannot be relabelled
as a pass for the patched plugin. Identify the actually loaded plugin for each
result; distinguish external operator evidence from locally executed tests and
record any missing provenance without inventing hashes, PNGs or a suite verdict.

Preserve failures and seek an authorized backend follow-up; do not repair packages
here, recreate resources as a workaround, or weaken assertions. This procedure
is not a claim that every installed plugin passes. See the
[rolling recovery operator guide](agent-undo-retention.md) for policy and lifetime
semantics independent of this native rendering limitation.

See [Image PNG workflows](image-png-exchange.md) for confinement, exact one-way
recovery and shared retention/approval policy; [Mesh qualification](mesh-device-qualification.md)
for its distinct silhouette/Array-binding assertions.
