# 26 — Navigate node placements with lifetime-safe handles

**What to build:** Complete bounded node navigation and local observations through the public headless and MCP interfaces, with real ABA-safe node lifetimes.

**Blocked by:** 32
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 4; Testing Decisions 2, 3, 4, and 12.
**Files:** src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/vsr/integration/mcp/README.md, src/vsr/core/Forest.hpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 03 before implementation. After 25, layers and roots have safe opaque handles, but only objects/layers have inspectable records. Forest nodes expose mutation, subtree copy/move, erasure, and pool-backed storage; those actual normal paths must establish lifetime, not a query-time snapshot.

## Acceptance criteria

- [ ] Implement inspection `kind: nodes` compatibly and advertise all three supported modes. Node queries support exact name, literal case-sensitive substring, exact node handle, node_kind, layer_handle, and immediate parent_handle; combined filters intersect. Reject unsupported/wrong-kind/malformed filters; stale exact/scoping handles do not redirect.
- [ ] Records expose node/layer handles, parent handle or root marker, stored name, kind, own enabled flag, and effective enablement from layer plus ancestors. Disabled/inactive nodes remain readable under scene read permission.
- [ ] Distinguish empty, scalar transform, object-reference, and transform-array nodes. Return complete local matrices for scalar transforms; reference nodes expose referenced object handle/type or explicit unavailable state. Array nodes expose reference/summary without mapping contents or selecting an arbitrary instance matrix.
- [ ] Traverse scene layers in order and nodes in per-layer pre-order with stable sibling order. Parent handles remain resolvable independently of a filtered page. Preserve all existing shared budgets, slice yielding, cancellation/deadline/scratch release and partial continuation semantics, including deep ancestry/effective-enablement work. Do not build an unbounded tree snapshot.
- [ ] Bind cursors to kind, complete filter set, policy, position, scene/launch and relevant inspection revision. Changed returned state stales cursors, historical replay remains historical, and sparse/no-match scans remain bounded.
- [ ] Every live node receives a host-issued domain-distinct handle. Erase/recreate in a reused slot with identical name/value between queries, ancestor erasure, layer removal/clear, scene reset/replacement, and copy/clone cannot redirect an old handle. Copy creates a distinct identity; moving the same live node preserves identity. Normal Forest/Layer/Scene mutation paths establish lifetime accurately; no final-value fingerprint or raw address/slot identity.
- [ ] Ordinary layer notifications update scene/render/inspection revisions without regenerating unchanged identities. Preserve object-only clients and existing object identity semantics.
- [ ] Public headless tests cover shared-object placements, duplicate names, all kinds, hidden ancestry, filters, direct/root/parent navigation, independent asymmetric local matrices, bounded paging, ABA/copy/move/removal/reset/replacement. Thin MCP tests demonstrate schema/parsing/records; full integration tests pass.

## Implementation and verification guidance

This is an observation/lifetime slice, not completion of 03: editable versions arrive in 27 and ancestry dependencies in 28. Do not emit fabricated edit/ancestry tokens, claim editing readiness, or substitute coarse layer invalidation for those later contracts. Establish actual creation/destruction/copy/move safety now.

Use confirmed public headless Integration safe-point tests, one red/green behavior at a time, with thin MCP checks and no test-only hook. Build with at most four jobs using progress.md guidance; logs go in `.scratch/broad-editing-logs/` outside the task subtree. Run changed cases, full `vsr::integration`, relevant existing Forest/Scene tests when core changes, and formatting; record commands/results.

## Out of scope

Fine node versions (27), ancestry tokens (28), numeric world/SRT derivation (04), node edits/restoration, new structural editing APIs, persistent IDs, bulk array reads, and broader refactors.

## Comments

### Orchestrator dependency update

25 was split before code into 31 (direct layer queries/lifetimes through both
headless and MCP) and 32 (name-filtered queries/combined verification). Wait for
terminal replacement 32; no identity or inspection requirement was removed.

### 2026-10-02 split from 03
Second vertical observational slice. Genuine normal-core lifetime tracking is required here; it cannot be deferred to fingerprints. Downstream 04/05 must wait for terminal replacement 28.
