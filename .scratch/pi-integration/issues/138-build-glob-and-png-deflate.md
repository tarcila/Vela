# 138 — Compile integration subdirectory sources and compress integration PNGs

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/CMakeLists.txt, src/vsr/integration/image/, src/tests/integration/test_IntegrationImage.cpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 02 (items 1–2). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

`src/vsr/integration/CMakeLists.txt` globs only top-level `.cpp` files, so subdirectory modules are header-only. `encodePngRgba8` emits stored (uncompressed) deflate blocks, about 4 MiB per 1024² image.

## Desired behavior

- `.cpp` files in integration subdirectories are compiled. A new subdirectory `.cpp` file builds without editing the CMake file.
- Integration PNGs are deflate-compressed through the vendored stb writer (linked privately) and remain byte-deterministic for a given input.
- `test_IntegrationImage.cpp` still passes, and a new check shows a 1024×1024 smooth-gradient image encodes well below 4 MiB.
- The full `[integration]` suite passes with MCP on and off.

## Out of scope

- Moving existing header-only modules into `.cpp` files.
- Any change to color mapping or orientation.

## Comments

### 2026-09-29 iteration — resolved
`vsr_integration` now uses `GLOB_RECURSE`, so subdirectory `.cpp` files compile; `encodePngRgba8` moved to new `image/PngEncode.cpp` (Paeth-filtered rows, `stbi_zlib_compress` q8, own IHDR/sRGB/IDAT/IEND chunks), linking `vela_ext_stb_image` privately. Chose `stbi_zlib_compress` over `stbi_write_png_to_mem` because the latter honours the process-global flip flag `SaveToFilePass` toggles and emits no sRGB chunk. `normalizeToPng` reports INVALID_DIMENSIONS if encoding fails (>2 GiB). New test: 1024² gradient < 1 MiB, byte-identical on re-encode, lossless decode.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && _out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (5458 assertions in 138 test cases) (MCP ON); same in `_out/_cmake_nomcp` (MCP OFF) → All tests passed (4816 assertions in 123 test cases)
Holes filed: none
