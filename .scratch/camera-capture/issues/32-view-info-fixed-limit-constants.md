# 32 — Triage fixed view-info limit duplication

**What to build:** Decide whether intentionally fixed policy literals and MCP description text should derive from a common constant set.

**Blocked by:** none
**Status:** wontfix
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Decision 5; finish Standards review.
**Files:** src/vsr/integration/inspection/ViewInfo.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp

## Finding

Standards-axis finish reviewer flagged `policyJson()` in `ViewInfo.hpp` and the view-info description in QueryTools.hpp: fixed limits (2048, 2073600, 1024, 120, etc.) occur in policy JSON and description prose. Proposed shared constexpr values to avoid inconsistent updates.

This is a low-priority duplication judgement, not a confirmed wrong advertised value. CODING_STANDARDS.md section 2 specifies constant naming; that alone does not prove all literals must be extracted. Pending configurable-policy ticket 20 may naturally settle representation after human blocker 24; do not bypass it or alter the frozen limits.

## Acceptance criteria

- [ ] Human accepts, defers to 20, or dismisses the duplication concern.
- [ ] If accepted, fixed metadata/description remain truthful and backend capture stays closed; tests verify unchanged effective values.

## Comments

Filed from required finish review of pinned trunk `e04c2853d04d3076b4f0870efc49f787bd870010` to `de2024eccc7e`. No implementation or automatic follow-up run authorized.

### 2026-10-03 human disposition
Folded into20's validated policy metadata/description consistency work; no standalone constants extraction. User approved this disposition.20 records the requirement; default limits remain unchanged. Closed as superseded, not as implemented.
