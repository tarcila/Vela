# 03 — Inspect layer nodes with safe live identities

**What to build:** Navigate the live layer hierarchy, distinguish placements from shared objects, and address a specific node with a safe opaque handle and state/dependency tokens rather than a name or pool index.

**Blocked by:** 01
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 4 and the identity/dependency requirements of 7; Testing Decisions 2, 3, 4, and 7.

## Current behavior

The Scene has layers and nodes, but integration inspection exposes only objects. Integration has object identities and scene/layer notifications, not externally addressable layer/node handles or precise node edit versions.

## Acceptance criteria

- [ ] Inspection kind selects objects/layers/nodes compatibly; node/layer queries support the specified filters, direct handles, deterministic ordering, bounded paging, and cursor binding.
- [ ] Layer records expose name/active/root; node records expose parent/layer/name/kind, own/effective enablement, local matrix where applicable, and object links or explicit unavailable states. Hidden nodes remain readable.
- [ ] Empty, scalar transform, object-reference, and transform-array nodes remain distinct; array instances are summarized, not mistaken for one scalar matrix.
- [ ] Host-issued layer/node identities distinguish domains and never redirect on deletion/recreation, copy, pool-slot reuse, layer removal, or scene replacement, including same-name/value ABA between queries.
- [ ] Fine node edit versions and opaque ancestry dependency tokens are observable. Target transform/enabled change-back advances tokens; rename/unrelated sibling changes do not invalidate target edits. Relevant parent-chain changes are detectable.
- [ ] Normal viewer/Lua/animation/importer/host mutations participate in accurate lifetime/version tracking. Final-state fingerprints alone do not establish absence of intervening writes; custom-host mutation requirements are documented.
- [ ] Scene/render/inspection revisions and cursor invalidation stay correct without wholesale regeneration of unchanged node identities.
- [ ] Public headless and thin MCP tests demonstrate hierarchy navigation, shared-object placements, precise versions, and lifetime safety.

## Out of scope

Node edit operations, world matrix/SRT derivation, structural editing, persistent IDs/archive changes, and whole-scene snapshots.

## Comments
