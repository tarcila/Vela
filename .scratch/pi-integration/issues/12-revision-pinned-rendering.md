# 12 — Add revision-pinned rendering through a registered view with helide

**Status:** resolved
**Milestone:** 1
**Blocked by:** 03, 04, 08, 10, 11
**Files:** src/vsr/integration/render/, src/tests/integration/test_IntegrationRender.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §12 (revision-pinned rendering), render deadlines in §5, and Behavioral acceptance coverage items 10 and 11 (render parts). Rendering acceptance must actually render with helide, as the RenderIndex tests do; a missing device is a failure, not evidence. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Views are registered, but no operation renders them.

## Desired behavior

A new suite `test_IntegrationRender.cpp`, using a real helide device and the procedural fixture from spec §8 (sphere above a ground plane, matte materials, one directional light, fixed camera), shows:

- A render names a view plus the desired scene/view tokens. A mismatch at the safe point returns `superseded` without rendering, and a relevant change mid-render also prevents success.
- Dimensions come from the view (1–1024 per axis). Invalid dimensions are rejected before backend work, without clamping or resizing.
- A scene that is not ready returns `sync_required` with no image, checked both at start and at completion.
- The 30-second deadline runs from admission.
- A cancelled or timed-out request never succeeds from a late image. Backend resources stay held until safe release, and `busy` is returned for necessary view resources.
- A successful result is a PNG of exactly the view's size with the matching tokens, stored through ticket 10's model. Replaying it is not a fresh capture.
- Tolerant image-content checks are used rather than bit-exact ones.

## Out of scope

- MCP mapping (ticket 15).
- Historical render snapshots and automatic resynchronization.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `src/vsr/integration/render/RevisionRender.hpp` (`RenderService` + host `RenderBackend`): token/readiness guards at start and completion, view-owned 1–1024 dimensions rejected pre-admission, 30 s deadline from admission, abandoned work held until safe release with `busy` for the view, success stored via `activity::ActivityLog`; replay keeps its capture id. New suite `test_IntegrationRender.cpp` renders the §8 fixture with a real helide device (missing device = REQUIRE failure; verified by running with an empty LD_LIBRARY_PATH → 7/7 failed). CMake exception used: `src/tests/integration/CMakeLists.txt` now links `vsr_rendering`.
Verified: `_out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (4378 assertions in 111 test cases); `[render]` alone → 209 assertions in 7 test cases.
Holes filed: 91-render-busy-and-dimension-codes
