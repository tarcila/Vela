# 24 — Decide the default policy's conservative encoder bound

**What to decide:** Reconcile the mandated conservative PNG encoding-bound validation with the existing encoder and the fixed default 2,073,600-pixel / 8 MiB PNG envelope before installing configurable policy metadata.

**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 5; Testing Decision 4.
**Files:** src/vsr/integration/inspection/ViewInfo.hpp, src/vsr/integration/image/PngEncode.cpp, external/vela_stb_image/stb_image_write.h (evidence only; no vendored change proposed)

**Kind:** contradicts

## Conflict

Decision 5 requires a conservative complete encoding bound for the default RGBA8 policy, configurable-policy validation against encoder overhead, and rejection of incompatible policy before installation. The defaults are 2,073,600 pixels and an 8 MiB (8,388,608-byte) encoded PNG cap. The current encoder cannot conservatively fit that whole envelope under this cap.

`src/vsr/integration/image/PngEncode.cpp:98–132` filters rows and calls `stbi_zlib_compress()`, then emits IHDR, sRGB, IDAT and IEND. The vendored writer uses fixed-Huffman deflate (`external/vela_stb_image/stb_image_write.h:882–990`), without a stored-block fallback. It is not zlib's `compressBound()` implementation. A zlib-library overhead formula would be an inaccurate bound on this encoder. Fixed-Huffman literal codes can require nine bits per filtered byte, in addition to the deflate/zlib and 70-byte PNG envelope.

This does **not** assert that no full-HD image fits, or that the spec guarantees every full-HD image succeeds. The conflict is specifically requiring the *conservative whole default encoding bound* to fit the PNG cap while retaining this encoder. The spec's actual-size `item_too_large` outcome handles publication failures, but does not explicitly decide whether a policy whose **known conservative bound exceeds its PNG cap** is nevertheless valid. Treating that as an exception to pre-installation compatibility validation is load-bearing, not a cheap metadata choice.

## Positive evidence

A headless deterministic probe called the public existing `image::encodePngRgba8(1920, 1080, rgba.data())` with 8,294,400 RGBA8 bytes. Start `uint32_t state = 0x12345678`; for each byte do `state ^= state << 13; state ^= state >> 17; state ^= state << 5; byte = uint8_t(state)`. No device/backend execution or file image publication occurs.

Command (local ignored probe source/logs under `.scratch/camera-capture-logs/`):

`c++ -std=c++17 -I src .scratch/camera-capture-logs/20-encoder-bound.cpp _out/_cmake/libvsr_integration.a _out/_cmake/libvela_ext_stb_image.a -o .scratch/camera-capture-logs/20-encoder-bound && .scratch/camera-capture-logs/20-encoder-bound`

Result: `pixels=2073600 png_bytes=8746547 default_png_cap=8388608`, exit 0 means the actual encoded PNG exceeds the default cap. Repeated after restoring/rebuilding/installing the baseline with the same result. Only byte count was printed; no PNG/base64 artifact was written.

## Decision needed

Choose and authorize the intended compatibility rule before resuming 20:

- Keep the defaults and explicitly allow an independent PNG publication cap below the conservative encoder bound. Specify validation/reservation behavior for the larger encoding scratch/output and expected `item_too_large` outcomes; do not call the cap a conservative bound on possible output.
- Or authorize a separately scoped encoder change/qualification that supplies a suitable conservative default bound, preserving image orientation/sRGB/alpha and existing rendering semantics.
- Or revise the default envelope/caps and associated transport/resource qualification explicitly.

Do not silently raise the PNG cap, narrow the pixel default, substitute a zlib bound for stb, or infer qualification/exposure from registered metadata. Capture remains closed. No assumption was installed.

## Acceptance criteria

- [x] Human records the authoritative rule: retain the envelope and8MiB cap; authorize a separately qualified bounded integration encoder.
- [x] Scope and compatibility decision recorded;20 waits for implementation33 before validating the actual bound. Decision resolution is not encoder completion.

## Comments

### 2026-10-03 iteration — ready-for-human
Found while implementing 20's first public headless policy-registration/update slice. Candidate tests passed for narrowed policy, tokens/replay, invalid updates and thin MCP, but inspection of the real encoder disproved the assumed zlib bound. Removed all candidate source/test changes; only ticket bookkeeping remains in this commit.
Verified: the compile/run probe command above → actual full-HD PNG 8,746,547 bytes exceeds 8,388,608; baseline full enabled build/install and all 268 integration cases pass (see 20's comment).
Holes filed: none (this is the blocking hole itself)

### 2026-10-03 human decision — resolved
User approved a bounded fallback repair in the integration encoder used by existing MCP vsr_render, retaining compression and repeat determinism and preserving decoded pixels/orientation/sRGB/alpha. Do not change vendored stb or CLI output paths. Keep all current dimensions/pixel/PNG/transport limits; discuss8MiB impact/increase after the two repairs. Implementation is33;20 is reblocked on33. No encoder or capture completion claimed by resolving this decision.
