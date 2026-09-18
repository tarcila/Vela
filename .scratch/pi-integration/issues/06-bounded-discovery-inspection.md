# 06 — Add bounded, paged target discovery and object/parameter inspection

**Status:** resolved
**Milestone:** 1
**Blocked by:** 03, 04
**Files:** src/vsr/integration/inspection/, src/tests/integration/test_IntegrationInspection.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §7 (bounded discovery and inspection), the discovery/capability rules in §4, the page and inspector limits in the §5 table, and Behavioral acceptance coverage item 9. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Handles, revisions and admission exist, but no operation lists targets or inspects objects.

## Desired behavior

A new suite `test_IntegrationInspection.cpp`, using real scenes, shows:

- Discovery and inspection are admitted scene operations. Each page consumes its own `request_id` and slot. Cooperative slices within a page keep that page's slot and yield between bounded steps.
- `page_size` defaults to 64 with a maximum of 256; invalid values are rejected, not clamped. Changing the size on a continuation does not by itself invalidate the cursor.
- Cursors are authenticated and bound to session, query, target and revision. Any returned-field change between or during pages returns `cursor_stale`, including a capability change on a listed discovery target.
- Discovery lists only readable targets and returns each target's current effective capabilities.
- Pages report the revision, visited/returned counts, and either completion or continuation with a stop reason (item, byte or work limit).
- Optional text truncation is marked. Correctness-bearing values are never truncated, and an item that cannot fit returns `item_too_large`.
- A large scene is enumerated without materializing the full list, respecting the item, byte, field, 4,096-visit, 256-slice and global 8 MiB scratch caps.
- The 5-second deadline yields `timeout`, not false completion. An identical-ID retry replays the retained page.

## Out of scope

- Bulk array export, filesystem browsing and full-scene snapshots.
- Transport and MCP mapping.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `inspection/Inspection.hpp` (Inspector: discovery + object inspection pages as admitted OperationEngine requests, SipHash-authenticated cursors bound to session/query/target/revision, sliced 256-visit yields, item/byte/4096-visit/8 MiB global scratch caps, 5 s deadline, truncation marking, item_too_large) and `test_IntegrationInspection.cpp` (page_size default/bounds/rejection, size change on continuation, identical-ID replay, yielded slot retention, work stop, timeout, tamper/session/query rejection, metadata and capability-change cursor_stale, readable-only discovery, truncation/oversize).
Not directly tested: mid-page revision change, scratch-cap exhaustion, fairness. No other modules modified.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ctest -C Release -R integration` → 100% passed (61 cases, 1955 assertions)
Holes filed: 55-inspection-item-shape
