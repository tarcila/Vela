# 08 — Capture through the selected viewport binding

**What to build:** Use the viewport's actual device/index and renderer/pipeline snapshot for a narrow preview capture, with truthful provenance and no interference with live viewing.

**Blocked by:** 04, 14
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decisions 1–2 and6–8; Testing Decisions 1 and6–8.

## Acceptance criteria

- [ ] Explicitly bind the selected viewer viewport and snapshot owned source settings at safe points; prefer existing actualdevice/index only when qualified, not a guessed library or cached load treated as a fresh device.
- [ ] Capture-owned camera/renderer/frame/pipeline preserve bit-identical source camera/manipulator/settings/dimensions/accumulation and unrelated revisions.
- [ ] Coordinate exclusive agent image ownership with legacy render without cancelling accepted work; interactive rendering remains active and busy is truthful.
- [ ] Qualify the narrow preview policy under concurrent main-view rendering, scene changes, cancellation and cleanup with actual heartbeat/latency/memory evidence.
- [ ] Unsafe/unresponsive/unavailable bindings stay unexposed with a reason; no automatic independent-device/process allocation or silent helide fallback.
- [ ] Viewer activity reports capture/progress/heldcleanup and actualbackend provenance; headless/MCP/viewer smoke proves images and unchanged live state under isolated configuration.

## Out of scope

Full target-policy certification (09/10), persistent viewport editing, renderer mirroring without dependency tracking and broad device-management changes.

## Comments

### Orchestrator dependency reconciliation

01 split into13 and14. This slice needs the real owning-thread viewer binding
from14 and the qualified capture path from04. Headless observation alone is
not evidence of a usable selected-viewport capture binding.
