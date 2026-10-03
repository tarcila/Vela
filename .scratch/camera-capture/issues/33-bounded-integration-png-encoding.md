# 33 — Bound integration PNG output without changing pixels

**Status:** resolved
**Milestone:** 1
**Blocked by:** 24
**Specification:** [Camera & Capture](../spec.md), Decision 5; authorized decision in 24.
**Files:** src/vsr/integration/image/ImageNormalization.hpp, src/vsr/integration/image/PngEncode.cpp, src/tests/integration/test_IntegrationImage.cpp

## Current behavior

The integration encoder filters RGBA8 rows and delegates compression to stb fixed-Huffman deflate. The deterministic high-entropy full-HD probe in24 produces 8,746,547 bytes, exceeding8MiB. normalizeToPng uses this encoder for existing MCP vsr_render. CLI renderer/file-pass PNG output is separate. Existing image tests independently decode semantic pixels and test compressibility and repeat determinism, not a golden encoding.

## Desired behavior

- [x] Publish a checked authoritative PNG output-bound helper usable by policy validation in20; describe dimensions/overflow/error semantics. Invalid/overflowing dimensions reject before pixel access/allocation. All supported outputs are at most the bound by construction, not via a guessed zlib-library formula.
- [x] Keep the fixed 2048 axis, 2,073,600 pixel and8MiB PNG defaults unchanged. Prove the bound fits the entire default envelope, including its worst permitted aspect ratio, not only1280x720/fullHD. Bound PNG chunks, deflate/zlib framing and arithmetic explicitly.
- [x] Add a deterministic bounded fallback in the integration encoder (stored-deflate is authorized). Retain existing compression for compressible inputs. A stored-block candidate bound is `(4*w+1)*h + 5*ceil(((4*w+1)*h)/65535) + 6 + 70`; derive/verify framing constants against actual chunk writer and handle all supported boundary cases. Check complete output and allocation arithmetic independently.
- [x] Existing MCP vsr_render may produce different encoded bytes, but decoded pixels, top-left orientation, straight alpha, sRGB chunk/intent, filtering semantics and repeat-call determinism stay unchanged. Do not edit vendored stb or separate CLI PNG paths. No capture exposure or backend qualification claim.
- [x] TDD independent decode-and-byte-compare for the24 xorshift full-HD input and worst default-envelope aspect ratio, retained gradient compression, deterministic repeat, stored-block boundaries/multiple blocks, zero/extreme dimensions and pre-read overflow rejection. Test bound values independently and exhaust all permitted heights/maximum widths arithmetically.
- [x] Reuse existing CUDA/UI/network/Lua/SDL3-enabled `_out/_cmake`; full build and fresh `_out` install before positive image/bound cases and full integration regressions. Record exact counts. Use available memory-safety verification where feasible; do not claim ASan/Valgrind ran if it did not, or cold-configure/disableCUDA.

## Out of scope

Increasing the8MiB cap, changing transport/defaults, configurable policy implementation20, capture lifecycle/quality, callback/core repair34, unrelated style findings30/31, vendored code and CLI renderer paths. No source outside Files, rawgit, remote publication or original pi-integration user artifacts.

## Comments

### Authorized human disposition

User approved preserving the envelope/cap and qualifying a bounded encoder repair, including its effect on existing MCP vsr_render while preserving image semantics. Discuss whether8MiB should increase **after these repairs**, using measurements/resource/transport impact; do not raise it during this ticket.24 records the decision, not a completed implementation. Complete this ticket before20 policy validation resumes. Planner's size formula is a candidate to prove, not authority to ignore errors or blindly use its proposed code shape.

### 2026-10-03 iteration — resolved
Implemented public `image::pngRgba8OutputBound(width,height)` with checked arithmetic and no pixel access/allocation; invalid dimensions reject before encoding and full normalization. Preserved Paeth, top-left RGBA8/straight alpha, sRGB perceptual intent, gradient compression and deterministic repeats; only integration encoder/header/tests changed, not defaults, vendored stb, CLI or capture exposure.
TDD reproduced 24's full-HD failure (8,746,547 bytes), then independently decoded identical bytes after bounded fallback (8,296,191 bytes). Five new public cases cover the full default envelope, entropy round trips, extreme aspect ratios, exact/partial/multiple stored blocks, PNG CRC/zlib Adler framing, and pre-read dimension/overflow rejection. No retry WIP was supplied.
Derived complete bound: `N=(4*w+1)*h`; each nonempty stored block contributes one padded header plus LEN/NLEN (5 bytes), with maximum payload 65535; zlib contributes 2+4 bytes; PNG signature/IHDR/sRGB/IDAT/IEND contribute 8+25+13+12+12=70. Hence `N+5*ceil(N/65535)+76`; successful compressed candidates are used only when no larger. Independently check filtered allocation, block arithmetic, complete output/container/int limits and final output size. stb signed-capacity growth is avoided for large inputs via direct stored encoding; allocation exceptions propagate.
All 2048 permitted heights at their maximum legal width were checked arithmetically; monotonicity in width covers every smaller width. The maximum is **8,297,136 bytes at 1024×2025**, not full-HD, with **91,472 bytes** below the unchanged 8,388,608 cap. Positive worst-envelope and 2048:1 / 1:2048 aspect-ratio images decode byte-for-byte. Near-INT_MAX helper arithmetic is tested without allocating gigantic images.
Verified: `cmake --build _out/_cmake -j 6` → full existing Release CUDA/UI/network/Lua/SDL3-enabled build passed; `cmake --install _out/_cmake --prefix "$PWD/_out"` → fresh install passed before positive cases/regressions. No reconfigure or CUDA disable.
Verified: `_out/_cmake/vsrIntegrationTests '[png-bound]'` → **5 new cases, 71,989 assertions passed**; `_out/_cmake/vsrIntegrationTests '[image]'` → **13 cases, 72,349 assertions passed** (includes all eight prior image cases).
Verified: `_out/_cmake/vsrIntegrationTests '~[png-bound]'` → **276 existing regression cases, 36,740 assertions passed**; `_out/_cmake/vsrIntegrationTests` → **281 cases, 108,533 assertions passed**; `ctest --test-dir _out/_cmake -R '^vsr::integration$' --output-on-failure` → **1/1 passed**. Counts are the actual separate runs; existing asynchronous assertion counts can vary.
Verified: `valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --log-file=.scratch/camera-capture-logs/33-valgrind.log _out/_cmake/vsrIntegrationTests '[image]'` → **13 cases, 72,349 assertions passed**, **0 errors**, zero definite/indirect/possible leaks; 2,318 bytes still reachable. ASan and backend/capture qualification were not run or claimed.
Verified: `clang-format --dry-run --Werror src/vsr/integration/image/PngEncode.cpp src/vsr/integration/image/ImageNormalization.hpp src/tests/integration/test_IntegrationImage.cpp` → passed. Bounded text evidence is ignored under `.scratch/camera-capture-logs/33-*`; no image/base64/binary artifact dumped. `jj diff --check` was unsupported by the installed jj (no source/check result claimed from it).
Holes filed: none
