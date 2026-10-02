# 31 — Inspect live layer/root identities through headless and MCP queries

**What to build:** The first complete vertical part of 25: host-issued lifetime-safe layer/root handles and public/MCP unfiltered/direct layer inspection, with existing bounded paging.

**Blocked by:** 01
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 4; Testing Decisions 2, 3, 4, and 12.
**Files:** src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/core/Forest.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp

## Current behavior

Split from 25 before code. 01/02 are resolved; queries and identity currently cover objects only. SceneIdentity receives layer add/remove/structure/transform/activation notifications, but currently uses these only to advance coarse revisions.

Inspection findings: Forest is non-copyable and non-movable. Forest::clear() calls erase_subtree(m_root), which erases descendants and preserves the live root. Forest::erase(root) likewise preserves that root. Layer::clear() currently delegates to Forest::clear() without issuing its own scene notification. Layer names come from name()/editableName(), not necessarily the Scene LayerMap key. Do not change root-preserving semantics merely to simplify identity.

## Acceptance criteria

- [ ] Add public inspection `kind: layers`; omitted kind stays `objects`, and `nodes` remains unsupported. Object result shape, permissions, admission, historical replay, and paging remain unchanged. Discovery advertises only implemented kinds.
- [ ] Layer records contain opaque host-issued layer handle, real stored name, active flag, and opaque root-node handle. Object/layer/node domains cannot alias; malformed/wrong-kind exact handles reject. Implement exact layer-handle queries now so lifetime behavior is testable through the agreed public seam. Stale exact handles conflict, never redirect.
- [ ] Establish actual lifetime participation at creation/destruction/reset/replacement, including remove/recreate between queries. Do not infer lifetimes solely from pointers, raw slots, names, snapshots, query-time comparison, or final values. Root handles name genuine live roots and are reusable by 26; no fabricated general-node observations or versions.
- [ ] Preserve identity for an unchanged live layer/root through ordinary notifications and root-preserving clear. If an actual supported reconstruction replaces a root, retire its old identity permanently. Clearing descendants must not resurrect a previously removed identity or suppress required scene notifications.
- [ ] Query layers in Scene layer order without full hierarchy snapshots. Preserve all existing page/item/optional/string/visit/slice/shared-scratch/deadline limits, cancellation and scratch release, and partial-page continuation semantics. Bind cursors to kind, supported filters, policy, scene/launch/revision and position.
- [ ] Activation/name/root/membership observations stale affected cursors; render-relevant changes advance scene revisions. Document/use the existing notifying host mutation contract, without regenerating unchanged handles.
- [ ] Public headless tests using real Scenes cover multiple/inactive/duplicate-stored-name layers, direct queries, paging, changed cursors, wrong-kind handles, layer ABA, reset/replacement, and actual root-preserving clear behavior. Existing object-only integration tests and relevant Forest/Scene regressions pass.

- [ ] Thin MCP queries expose the implemented objects/layers modes, parse kind and exact handle safely, and return layer fields end-to-end. Unsupported nodes and unsupported name filters reject at this intermediate stage. No headless-only slice is claimed complete.

## Implementation and verification guidance

Use the agreed public Integration safe-point seam, TDD one red/green behavior at a time, and no test-only hooks. This part excludes name filters, completed by 32. Thin MCP kind parsing/schema and returned layer records are included here so the slice is end-to-end. Reject unsupported filters at this stage rather than silently ignoring them. Reuse the existing query engine and bounded traversal; inspect actual lifecycle paths before choosing minimum core support. Core changes are permitted only when required for genuine layer/root lifetime correctness, not general node features from 26.

Build with at most four jobs using progress.md's minimal flags and this workspace's own CMake paths; never copy caches. Capture verbose logs under `.scratch/broad-editing-logs/` outside the task subtree. Record exact targeted/full integration tests, relevant core regressions, formatting commands, and deliverable/scratch IDs. Read 32 to preserve its extension seam; it is not implemented by this ticket.

## Out of scope

Name/name_contains layer filters and their MCP wiring (32); general nodes (26), fine versions (27), dependencies (28), transforms/edits/restoration, archive IDs, and unrelated refactors.

## Comments

### 2026-10-02 split from 25
First tracer-bullet part: safe lifetimes plus bounded unfiltered/direct public layer queries. No code was started by the splitting iteration. 32 completes the original 25 contract; downstream 26 must wait for 32 rather than treating this part as completion of 25.
