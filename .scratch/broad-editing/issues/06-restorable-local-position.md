# 06 — Move transform nodes locally and restore exactly

**What to build:** Move one or several existing transform placements in local space while preserving their linear matrix components and enabling exact batch restoration.

**Blocked by:** 05
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for local position; Testing Decisions 5, 7–10, and 12.

## Current behavior

The enabled-state slice provides node admission, version checks, atomic commit, synchronization, activity, and restoration. Position is still outside the node edit surface.

## Acceptance criteria

- [ ] Local position accepts exactly three finite representable components; omitted space means local. Unsupported node kinds and malformed values reject without changing any target.
- [ ] Replace only the translation column; preserve every other stored matrix component, including shear/reflection/degeneracy, and preserve default/reset behavior.
- [ ] Multi-node position batches and position-plus-enabled groups are atomic, bounded, version pinned, independent of entry order, and restorable.
- [ ] Later target writes, change-back, reparenting, ancestor removal, and identity replacement conflict. Unrelated siblings and parent transform changes alone do not block local-space edits.
- [ ] Preparation failure emits no mutation notifications or committed journal. Ordinary notifications/synchronization and result/version reporting use the preceding complete node path.
- [ ] Restore exact prior transform/representation for changed fields only; keep unrelated state and existing conflict/retention/authority semantics.
- [ ] Headless and thin MCP tests demonstrate movement and restoration with independently worked positions and nontrivial original matrices.

## Out of scope

World positioning, SRT/full-matrix replacement, structural editing, camera/view movement, and broader object writes.

## Comments
