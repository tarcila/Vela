# 05: Versioned selection-translation preview

**What to build:** Agents preview once-only world-space movement of an explicit selection using real edit preparation, normalized selected roots and a bounded exact prepared plan, without mutating the Scene.
**Blocked by:** 01
**Status:** resolved
**Milestone:** 1
**Specification:** [Truthful Views, Presented Frames and Safe Selection Translation](../spec.md), stories75–95/107–108 and decisions16–20.

## Current behavior

Low-level versioned local/world edits exist, with node-origin and ancestry semantics. An agent can mistakenly apply the same local delta to an ancestor and child.01 supplies explicit examples and request diagnostics. Verified identity/ownership/invalidation/edit preparation is in the approved source base; do not restart it or invent a chess assembly abstraction.

## Acceptance criteria

- [x] Add admitted vsr_translate_preview through the public headless/MCP seam using one shared real Translation/edit-preparation path, not a second simulator. Inputs include one Scene, bounded selection, fresh node-edit/structural/ancestry expectations and finite world delta; enforce current read/edit authority/exposure without protected-payload leakage.
- [x] Selection is deterministic: identical duplicates collapse with disclosure; conflicting expectations reject. Normalize only topmost **selected** roots; selected descendants inherit once and remain validated. Never promote to an unselected piece/common parent. Eligible scalar-transform roots only; no wrappers/topology creation.
- [x] Preserve each root's linear transform and move its existing origin by the world delta through established parent-affine conversion. Rotation/shear/reflection/local signed-zero scales retain supported position semantics. Singular required parents, nonfinite computation and float32 overflow reject all; ordinary fractions follow existing rounding, not exact binary round-trip rejection.
- [x] Return current/predicted origins, prepared matrices, normalized roots, inherited selected nodes, dependency expectations, bounded effect warnings and opaque prepared reference. Unselected descendants naturally inherit root movement; unenumerated/truncated impact lists/counts are explicit. No completeScene fingerprint/scan or assumed geometry centre.
- [x] Preview changes no Scene/view/renderer, mutation revision, Batch Reference or restore eligibility. Bound ancestry/effect work and all preparation allocations before publishing a truthful result.
- [x] Edit-owned preview retention binds launch/session/Scene/input/dependencies with validated advertised finite per-session/global counts, aggregate bytes, expiry/deadline and selection<=64(or narrowerhostlimit). SessionAuthority stores no Scene matrices/plans. Expiry/eviction/withdrawal/teardown cannot revive tokens or dangle ownership; no global map/shared-store migration.
- [x] Public realScene tests cover nested roots, sibling/independent parts, duplicates, foreign/stale/unreadable selection, ineligible roots, rotated/sheared/reflected/zero-scale/singular/overflow/fractional maths, exact negative mutation controls and preview exhaustion/lifetime. Test actual transported shapes and privacy/replay behavior.
- [x] Build/install enabled config, focused/full applicable edit/identity/protocol regressions, formatting and preview owner/eviction memory checks pass; record actual evidence. Preview knowledge is visible through the actual client seam, not an unseen resource alone.

## Out of scope

Translation commit06, group rotation/scaling/snapping, piece/chess inference, bounds-centre edits, image/view-frame work, persistent structure creation, user live edits, historical tracker mutation and arbitrary new authority.

## Comments

Depends on01's request/semantics surface and already present verified edit foundation, not02–04. Keep related source factoring within this complete preview behavior. Final independent integration review is orchestrator-owned; author cannot label preparation validation as independent review or claim preview alone completes selection movement.

### 2026-10-04 iteration — resolved
Added admitted, tool-visible `vsr_translate_preview` with real shared `EditService::prepare()` and exact matrix/SRT retention. Validates every selected identity/dependency before deterministic topmost-selected normalization; duplicate disclosure, inherited origins, independent siblings, affine conversion/rounding and atomic rejection are covered. No Scene/view/renderer/version write, Batch Reference or restore eligibility is introduced.
The edit-owned bounded store has current read/edit/exposure gates, launch/session/input/dependency binding, source/EditService owner guards and safe-point internal access for06. Defaults are64 selections,8/session and32/global retained plans,16MiB aggregate charges,4096 ancestry steps,64 selected effects,120s TTL and5s deadline. Charges are64KiB owner,512KiB preparation and128KiB per held result copy; tests actually exercise64 roots,1-plan per-session/global caps and704KiB exhaustion/release. Unselected descendants are explicitly not enumerated. Historical receipts and their shared budget survive source/service/prerequisite retirement independently of expired/evicted plans, without token revival.
Verified (workspace root; runtimes prepend `$PWD/_out/lib` to `LD_LIBRARY_PATH` and set workspace-local `TMPDIR`; all full tests serialized):
- `cmake --build _out/_cmake --config RelWithDebInfo -j3` then `cmake --install _out/_cmake --config RelWithDebInfo` → exits0, existing enabled CUDA/UI/network/Lua/SDL3/integration/MCP configuration retained. `clang-format --dry-run --Werror` on all eight changed C++ files →exit0.
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-preview]'` →1133 assertions/9 cases; actual MCP HTTP discovery/transport/status/replay/privacy plus real-Scene math/normalization/authority/negative mutation and ownership/budget/lifetime coverage.
- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` →68/68 pass,72.68s; integration binary `'[integration]'` →125061 assertions/349 cases,exit0 (polling totals vary).
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --log-file=.scratch/viewport-integration/logs/05-valgrind-final.txt _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-preview],[node-edit],[node-position]'` →7944 assertions/34 cases,exit0,0 errors and0 definite/indirect lost bytes.2376 possibly-lost bytes/15 blocks,4186641 reachable bytes/180 blocks and driver warnings remain disclosed; no suppressions, sanitizer or GPU-memory claim.
- Workspace build-tree `_out/_cmake/RelWithDebInfo/vsrTutorialIntegrationHeadless --exit-after-activation` after library installation →exit0, isolated procedural Scene. This configuration does not install an `_out/bin` executable/header SDK; an absent-path invocation was not a passing smoke.
Evidence, reservation proof, exact log pointers, intermediate failed attempts and remaining limits: [05-verification.md](../05-verification.md). Source parent/bookmark equal dispatched `508c95842263a578920f173539066cd05df64e7f`; immutable spec hash unchanged. No04/06, other ticket/config/workspace or live user Scene/controller changes. No barney/CUDA presentation, upstream model-generation or complete selection-commit qualification is claimed. **Final independent whole-spec review remains pending**, supplied by the orchestrator after merges; author validation is not independent review.
Holes filed: none. No splits/new tickets. Exact base-workflow outcome JSON is committed in [05-outcome.json](../05-outcome.json).
