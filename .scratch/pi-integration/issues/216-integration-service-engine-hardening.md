# 216 — Serialize Integration::service, bound its queue, fix the engine↔integration lock order, the dispatch-failure slot leak and zero-scene discovery

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/Integration.hpp, src/vsr/integration/Integration.cpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/activity/Activity.hpp, src/tests/integration/test_Registration.cpp, src/tests/integration/test_IntegrationOperations.cpp

Source: post-review round `post-review:1:1` (p1 report 58e8416b: gpt/finding-1, gpt/finding-2, gpt/finding-3, grok/nested-service-clears-servicing-thread, grok/deadlock-engine-integration, grok/timing-data-race, grok/dispatch-fail-stuck-slot; spec-compliance pass: (a) session-wide discovery with zero registered scenes). The human accepted these findings as fixes on 2026-09-28; see `.scratch/pi-integration/pipeline.md`, §Partition post-review:1:1. Contract: `.scratch/pi-integration/spec.md` plus addenda 01–08.

## Current behavior

- `Integration::service()` protects only the batch swap. Two concurrent or nested service calls can run scene work at the same time and overwrite `m_servicingThread`.
- A `SceneWork` that throws abandons the rest of its batch and leaves the servicing thread set.
- `submit()` has no queue bound.
- `timing()` returns an unsynchronized reference.
- An `ActivitySink` or `sceneOfView` call made under the engine mutex can invert lock order against `service()` → `runStep`.
- A failed `Integration::submit` after admission leaves the record QUEUED and holding its slot.
- `OperationEngine::sceneFor` hard-codes `SceneId{0}` for session-wide work, so discovery cannot run when no scene is registered.

## Desired behavior

- Only one thread services at a time. A concurrent `service()` either blocks or returns 0 without running work. A nested `service()` from inside work returns 0. `inServiceCall()` stays correct throughout. Document the choice in `Integration.hpp`.
- An exception thrown by work is caught per item and logged without secrets. The servicing state is always reset, and the remaining items in the batch still run.
- `submit()` has a configurable finite bound, with a documented default, and returns false when full.
- `timing()` is safe to read concurrently with `activate()`, for example by returning a copy under the lock.
- No engine callback, sink or `Integration` query runs while the engine mutex is held in a way that can invert against `service()`. Document the lock order in `Operations.hpp`, and add a test that runs concurrent RENDER-kind submits alongside `service()` with a sink installed.
- If `submit` fails after admission, the operation finishes terminally, the slot is released and the activity entry is updated.
- Session-wide discovery works with zero registered scenes and returns an empty completed page.
- Tests cover every item above. The full `[integration]` suite passes.

## Out of scope

- Scene-operation modules (ticket 217).
- MCP (tickets 214/215).

## Comments

### 2026-09-29 iteration — resolved
Integration: service() is exclusive and non-blocking (concurrent/nested call returns 0; documented in Integration.hpp); per-item try/catch logs a fixed message (no exception text); bounded queue (ctor arg, DEFAULT_QUEUE_CAPACITY 1024, submit false when full); timing() returns a copy under the lock; new submitSessionWork() for scene-less safe-point work. Engine: lock order documented in Operations.hpp (engine → sink → Integration leaf; Integration mutex never held while work runs, so no inversion); dispatch() returns bool and finishes an undispatchable step (QUEUED → busy, started → cancelled; committed/BACKEND left alone), releasing the slot and updating activity; session-wide work with zero scenes runs via submitSessionWork on an engine-owned placeholder Scene, discovery returns an empty complete page. Activity.hpp needed no change. Deliverable change id: rsswppqlpzzs.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests` then `_out/_cmake/Release/vsrIntegrationTests "[integration]"` ×5 → All tests passed (160 test cases, ~6000 assertions each run)
Holes filed: none

