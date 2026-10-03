# 07 — Bound larger captures and complete transport costs

**What to build:** Request declared capture dimensions including1280×720 without resizing the viewport, while complete host/device/encoding/retention costs stay inside advertised policy.

**Blocked by:** 04
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 5–8; Testing Decisions 4–7.

## Acceptance criteria

- [ ] Implement validated axis/pixel/quality/deadline and per-view/global memory/capacity limits with checked arithmetic; default pixel maximum2073600 supports target and fullHD, not unrestricted2048².
- [ ] Internally quote/revalidate explicit frame/AOV/scratch/normalization/PNG/base64/JSON/retention costs and atomically reserve before setup. Distinguish honest driver estimates/unknowns from enforced bounds.
- [ ] Complete responses obey PNG8MiB/body12MiB defaults below proxy16MiB. Known failures reject before allocation; actual encoding overflow yields item_too_large with no truncation, resize or quality reduction.
- [ ] Captures share existing retained-image budgets; active memory is protected and historical image expiry is visible without recapture.
- [ ] Source dimensions/renderer/pipeline and accepted legacy rendering remain unchanged; held cleanup capacity is not reused.
- [ ] Headless/MCP tests cover limits/overflow, target/fullHD dimensions, failures/retention/reservations and independently correct output buffers; real CPU larger-size latency/memory qualification is recorded.

## Out of scope

Viewport resizing, unlimited images/history, arbitrary device selection, global proxy-cap weakening and architectural migration.

## Comments
