# 217 — Fix edit replay canonicalization, the render pending-success re-guard, the PNG buffer leak and the inspection scratch leak

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/render/RevisionRender.hpp, src/vsr/integration/image/PngEncode.cpp, src/vsr/integration/inspection/Inspection.hpp, src/tests/integration/test_IntegrationEdit.cpp, src/tests/integration/test_IntegrationRender.cpp, src/tests/integration/test_IntegrationImage.cpp, src/tests/integration/test_IntegrationInspection.cpp

Source: post-review round `post-review:1:1` (p2 report 16ca739a: gpt/finding-1, gpt/finding-2, gpt/finding-3, coordinator scratch-leak note). The human accepted these findings as fixes on 2026-09-28; see `.scratch/pi-integration/pipeline.md`, §Partition post-review:1:1. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–08.

## Current behavior

- `EditService::canonicalArguments` encodes non-number, non-array JSON values by kind only.
- `RenderService`'s pending-OK path reports success after a delayed safe release without re-checking tokens and sync readiness.
- `encodePngRgba8` frees the stb zlib buffer only on the success path.
- Inspection scratch is released only on `Status::COMPLETE`, so abandoned, cancelled or timed-out jobs may leak global scratch.

## Desired behavior

- Canonical arguments encode the full content of every JSON kind: strings escaped, booleans, null, and objects with sorted keys. A test shows two edits whose values differ only in a non-number kind are not treated as replays.
- On the pending-OK path the render re-checks the scene and view tokens and `viewReady()` before completing. A mismatch or loss of readiness returns `superseded` or `sync_required`, with no image. A test holds the release, changes the view, then releases.
- The PNG encoder owns the zlib buffer exception-safely, for example with a `unique_ptr` and `free`.
- Scratch reservations are released on every terminal path of an inspection job: complete, cancelled, timed out and session ended. A test shows the global scratch usage returns to 0 after cancelled and timed-out pages.

## Out of scope

- Engine and Integration changes (ticket 216).

## Comments

### 2026-09-29 iteration — resolved
Deliverable loqxntvq. Canonical edit args now encode strings (length-prefixed), booleans, null, and objects (sorted keys); `editValue`/`JsonValue` now carry that content (outside Files, see 237). Render pending-OK path re-checks tokens + `viewReady()` after delayed release → superseded/sync_required, no image. PNG zlib buffer held in `unique_ptr<…, free>`. Inspection `Job` owns its scratch reservation and releases it in its destructor, so cancelled, timed-out and session-ended jobs free it when the engine drops the executor.
New tests: edit canonical/non-replay scenario; render held-release view/scene/readiness sections (fail 12/48 with fix disabled); inspection scratch on cancel/timeout/session-end (fails with RAII disabled). PNG: covered by existing image round-trip tests (no alloc-failure seam).
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && _out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (6297 assertions in 171 test cases)
Holes filed: 237-edit-value-content-scope
