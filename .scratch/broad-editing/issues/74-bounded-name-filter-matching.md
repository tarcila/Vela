# 74 — Yield bounded name-filter matching on oversized stored names

**What to build:** Keep exact/substring filtering correct on real stored names without allowing one oversized name to monopolize a safe-point slice or bypass inspection work limits.

**Blocked by:** 01
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 (literal matching against stored names, bounded matching slices and actual traversal work); Testing Decisions 3 and 11.

## Current behavior

Implementation audit found object, layer, and node substring predicates calling the stored string's unbounded find operation directly. Input filter strings are capped, but stored names need not be; bounding node visits alone does not bound this work. Existing tests exercise sparse registries, not oversized names whose match is beyond the display preview.

## Acceptance criteria

- [ ] Match exact/literal case-sensitive substring predicates against the real stored name, including a match beyond the display preview, without treating truncated preview text as authoritative.
- [ ] Use bounded resumable matching work across safe-point slices and page continuation. Avoid pathological repeated-prefix/quadratic comparisons; charge query scratch and actual scanning to explicit bounded work policy.
- [ ] Objects, layers, and nodes share consistent behavior. Empty substring, no-match, combined filters, direct handles, and deterministic unchanged-scene ordering remain compatible.
- [ ] Continuations capture any needed partial-name progress and bind it to the complete query/revision; do not restart an unbounded scan on each continuation or skip a partly scanned target.
- [ ] Cancellation, deadline, visit/work exhaustion, cursor staleness, scratch release, historical replay, and all existing page/item/string limits stay truthful. Do not solve the problem by silently rejecting every long stored name or imposing a new write restriction.
- [ ] Public headless tests with oversized names, beyond-preview matches, repeated-prefix adversarial strings, small slice/work budgets, cancellation, continuation, and all three query kinds demonstrate bounded progress and correct results; thin MCP tests retain supported schema and results.
- [ ] Full integration/CTest and formatting pass. Record exact commands and actual policy units in documentation if additional scan accounting is introduced.

## Out of scope

Regex/glob matching, Unicode normalization, new edit capabilities, changing existing node identity semantics, spec weakening, and architectural cleanup.

## Comments

### Orchestrator audit

This is completion of an existing approved bounded-inspection requirement, not a new feature or an undecided spec hole. Final verification 12 waits for this fix. The new number is the next actual ticket after 73; prior implementer reservations are not dummy issues.
