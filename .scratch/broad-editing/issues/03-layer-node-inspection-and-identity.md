# 03 — Inspect layer nodes with safe live identities

**What to build:** Navigate the live layer hierarchy, distinguish placements from shared objects, and address a specific node with a safe opaque handle and state/dependency tokens rather than a name or pool index.

**Blocked by:** 01
**Status:** wontfix
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

### 2026-10-02 iteration — wontfix
split into 25, 26, 27, 28 before code: layer/root observations → node observations/lifetimes → fine editable versions → structural/ancestry dependencies. All parts retain Milestone 1, public headless TDD plus thin MCP coverage, and the original strict lifetime/change-back/bounded-work requirements; no criterion is marked satisfied by this split.
Inspection/identity are currently object-only; LayerNodeData setters/assignment lack fine mutation history, and Forest copy/move/erase require genuine lifetime/topology coverage. This cannot safely fit one 1200-second sitting without weakening the contract. No implementation or tests changed; no retry WIP exists.
Downstream 04 and 05 must be reblocked by the orchestrator on terminal replacement 28, not treated as unblocked by 03 being wontfix. Sibling tickets/spec/addenda/user files were not edited. Numbers 29–30 remain unused.
Verified: `~/.local/share/agent-hooks/scratch-commit.sh preflight broad-editing` → passed; `python3 -c 'from pathlib import Path; p=Path(".scratch/broad-editing/issues"); t=[next(p.glob(str(n)+"-*.md")).read_text() for n in range(25,29)]; assert all("**Status:** ready-for-agent" in s and "**Milestone:** 1" in s and "**Files:**" in s and "## Acceptance criteria" in s for s in t); assert all("**Blocked by:** "+b+"\n" in s for s,b in zip(t,["01","25","26","27"]))'` → passed (four complete ready tickets with ordered blocking edges). Scratch-only split: no build/test pass is claimed.
Holes filed: none
