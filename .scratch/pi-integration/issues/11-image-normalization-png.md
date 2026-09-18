# 11 — Add image normalization to 8-bit straight-alpha sRGB PNG

**Status:** resolved
**Milestone:** 1
**Blocked by:** 01
**Files:** src/vsr/integration/image/, src/tests/integration/test_IntegrationImage.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §12 (the normalization and color-mapping paragraphs, and returned-image ownership) and the color-mapping part of Behavioral acceptance coverage item 10. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

The existing save-to-file pass writes files but does not implement the contract's color/alpha mapping or in-memory PNG results.

## Desired behavior

A new suite `test_IntegrationImage.cpp`, using controlled synthetic buffers, shows:

- Output is an in-memory PNG, RGBA 8-bit, top-left origin, verified with an asymmetric test image.
- Float linear color is clamped to [0,1] (NaN maps to 0), sRGB-encoded, and rounded as ×255 with exact halves rounding up.
- Alpha is clamped (NaN maps to 0) and quantized by the same rule.
- An 8-bit sRGB straight-alpha source passes through unchanged.
- A premultiplied source is converted to straight alpha in linear space, including decoding 8-bit sRGB premultiplied input first; RGB is 0 where alpha is 0.
- The result owns its bytes, independent of the source buffer's lifetime.

## Out of scope

- Rendering, the backend and revision checks (ticket 12).
- Changes to imported textures, `SaveToFilePass` or Image Cache orientation.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `src/vsr/integration/image/ImageNormalization.hpp` (following the ticket-19 convention): `normalizeToPng(SourceImage)` maps float-linear or 8-bit sRGB sources, straight or premultiplied, with top-left or bottom-left row origin, to owned PNG bytes (RGBA8, sRGB chunk, top-left origin). Its self-contained encoder uses stored deflate blocks, so no shared CMake edit was needed. New suite `src/tests/integration/test_IntegrationImage.cpp` decodes the results with the vendored stb_image, included by relative path as a static, TU-private copy. It covers: an asymmetric orientation oracle; clamping and NaN handling; the sRGB mid-gray case (188); rounding of exact halves; alpha quantization, including the 0.5→128 midpoint; byte-exact passthrough; premultiplied float and 8-bit sRGB input unpremultiplied in linear space; alpha=0 giving RGB=0; and bytes still valid after the source buffer is reused or freed. Tone mapping is left out, since the pipeline owns it.
Verified: `_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (981 assertions in 35 test cases); `ctest -C Release -R integration` → 1/1 passed.
Holes filed: 43-png-uncompressed-encoding
