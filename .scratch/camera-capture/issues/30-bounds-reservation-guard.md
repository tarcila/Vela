# 30 — Triage bounds reservation guard clarity

**What to build:** Decide whether to replace the full `Job` used as a temporary scratch-reservation guard with a small explicit RAII guard.

**Blocked by:** none
**Status:** needs-triage
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Decision 3; finish Standards review.
**Files:** src/vsr/integration/inspection/Inspection.hpp, src/tests/integration/test_IntegrationInspection.cpp

## Finding

Standards-axis finish reviewer flagged `Inspector::makeBounds` in `Inspection.hpp`: `Job reservation` only uses `pool` and `reserved`, then transfers ownership by zeroing `reserved`. Its other query/filter/string/vector members obscure the exception-safety purpose. Proposed dedicated `ScratchGuard{pool, bytes}`.

This is a judgement-call Mysterious Name/Middle Man smell, not established production breakage or a mandatory documented-standard violation. Reviewer's general citation was CODING_STANDARDS.md section 5; triage must validate whether refactoring is worthwhile. Preserve allocation-failure and cancellation accounting.

## Acceptance criteria

- [ ] Human decides whether the readability finding warrants a change.
- [ ] If accepted, reservation ownership remains exception-safe and exactly once; bounds/shared scratch and integration regressions pass after fresh build/install.

## Comments

Filed from required finish review of pinned trunk `e04c2853d04d3076b4f0870efc49f787bd870010` to `de2024eccc7e`. No implementation or automatic follow-up run authorized.

### 2026-10-03 human disposition — deferred
User agreed this is readability cleanup, not demonstrated allocation-accounting breakage. Defer until functional bounds/capture work; do not gate the repairs or expose it as a required correctness fix. Remains a parked triage record, not ready-for-agent.
