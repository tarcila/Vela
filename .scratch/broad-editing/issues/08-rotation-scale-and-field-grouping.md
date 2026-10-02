# 08 — Edit faithful rotation/scale and group node fields

**What to build:** Rotate/scale existing transform placements consistently with VSR and combine position, rotation, scale, and enablement predictably without losing imported matrix state.

**Blocked by:** 04, 06, 07
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for conveniences/grouping; Testing Decisions 5, 8, 10, and 12.

## Current behavior

Read affordances, local position, and explicit matrix replacement exist. Rotation/scale conveniences and the complete grouped-field contract are not yet implemented.

## Acceptance criteria

- [ ] rotation uses named azimuth/elevation/roll degree components and VSR's axes/order; scale uses three finite components. Zero/signed components are allowed only when resulting state is faithfully represented.
- [ ] Reject non-finite/out-of-range inputs and unfaithful existing SRT reconstruction under the specified tolerance. Do not silently remove shear or otherwise normalize imported state.
- [ ] Partial edits preserve translation unless position is specified. Retain supplied convenience angles consistently with VSR and full semantic request identity.
- [ ] Group different fields per target with one consistent expected version; repeated fields and matrix-plus-SRT/position combinations reject before mutation.
- [ ] Build proposed linear state then translation/enablement independently of entry order. Existing matrix/position/enablement behavior remains compatible.
- [ ] Exact restoration preserves prior matrix/representation and default baseline, with fine conflicts, bounded journal, synchronization, and activity.
- [ ] Headless and thin MCP tests cover independent angle/scale oracles, shear/reflection/degeneracy, field permutations, conflicts, and restoration.

## Out of scope

World-space rotation/scale, silent repairs, creation/wrappers, broader parameter writes, and generic result-store changes.

## Comments
