# 31 — Triage query schema validation consistency

**What to build:** Decide whether sibling MCP query tools should share a documented unknown-argument/schema convention.

**Blocked by:** none
**Status:** needs-triage
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), thin MCP adapter; finish Standards review.
**Files:** src/vsr/integration/mcp/tools/QueryTools.hpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Finding

Standards-axis finish reviewer flagged `QueryTools.hpp`: `vsr_bounds` walks argument keys and advertises `additionalProperties:false`, while sibling `vsr_inspect`, `vsr_discover` and `vsr_view_info` schemas do not all advertise that flag. This is an optional consistency/Shotgun Surgery concern, not a demonstrated authority or validation defect and not a documented-standard violation.

Do not weaken strict validation or change accepted semantic request identity just for stylistic uniformity. Verify actual sibling handler behavior during triage rather than assuming schema omission allows unknown input.

## Acceptance criteria

- [ ] Human decides whether to unify or document the convention, or dismiss the finding.
- [ ] Any accepted change preserves tool compatibility, pre-admission rejection and replay behavior, with public MCP regression tests and fresh build/install.

## Comments

Filed from required finish review of pinned trunk `e04c2853d04d3076b4f0870efc49f787bd870010` to `de2024eccc7e`. No implementation or automatic follow-up run authorized.

### 2026-10-03 human disposition — deferred
User agreed to defer schema unification. Establish an actual handler/schema mismatch before any compatibility-changing fix; otherwise documentation/cleanup only. Do not gate the repairs. Remains a parked triage record, not ready-for-agent.
