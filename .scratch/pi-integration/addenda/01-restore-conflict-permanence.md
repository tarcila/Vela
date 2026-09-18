# Addendum 01 — A restore conflict permanently ends a batch's eligibility

**Extends:** spec §11 (One-way Batch Restoration), overriding one clause of it. **Source:** hole ticket 137, raised by ticket 121. **Classification:** contradicts. **Decided by:** human, 2026-09-28, option (a).

## Undecided or contradicted
Spec §11 says "Preparation/conflict failure changes neither values, versions, nor original eligibility". Ticket 121 instead makes the first restore `conflict` mark the batch `CONFLICTING`, release its restoration payload and free its restorable slot, so every later retry also reports `conflict`.

## Decision
The implemented behavior stands. A restore that fails its version/identity precondition (`conflict`):
- changes no scene values or versions;
- permanently marks the batch `CONFLICTING` in the activity log;
- releases its restoration payload and its slot among the 32 restorable batches;
- makes every later restore of that batch report `conflict`.

A restore **preparation** failure (`busy`) still leaves eligibility unchanged. The "Preparation/conflict failure … nor original eligibility" clause of §11 now applies to preparation failure only.

## Why
Object parameter versions only advance, and even a change back to the original value produces a new version. So a batch that conflicts once can never pass the version check again. Keeping it "eligible" would hold restoration storage for a record that cannot restore, and would present a recovery option in the viewer that always fails.
