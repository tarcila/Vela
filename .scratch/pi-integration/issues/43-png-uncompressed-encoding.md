# 43 — Integration PNGs use uncompressed (stored) deflate blocks

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/image/ImageNormalization.hpp, src/vsr/integration/CMakeLists.txt

**Kind:** extends

The spec requires PNG output but does not choose an encoder or compression level. Ticket 11 wrote a self-contained header-only encoder (`encodePngRgba8`) that emits stored deflate blocks, filter 0, and an `sRGB` chunk. It keeps `vsr_integration` free of new link dependencies: `src/vsr/integration/CMakeLists.txt` links only `vsr_scene Threads::Threads`, and linking `vela_ext_stb_image` (for `stbi_write_png_to_mem`) would need an edit to that shared file.

Consequence: a 1024×1024 result is about 4 MiB, so the 32 MiB retained-image budget holds about 8 maximum-size images, not the 30-plus that compressed output would allow.

Assumption: correctness first; the size is still bounded and deterministic. Revisit by linking `vela_ext_stb_image` in `src/vsr/integration/CMakeLists.txt` (`project_link_libraries(PRIVATE vela_ext_stb_image)`) and swapping the body of `encodePngRgba8` for `stbi_write_png_to_mem`, or by adding a real deflate. The `test_IntegrationImage.cpp` oracle decodes with stb_image and needs no change.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 02 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 138.
