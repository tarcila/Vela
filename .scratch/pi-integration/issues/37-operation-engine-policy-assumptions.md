# 37 — Operation engine policy assumptions (control priority, envelope code, local admission)

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/operations/Operations.hpp

**Kind:** extends

Ticket 03 had to settle these points, which the spec leaves open. Each is cheap to change.

1. **Control priority and coalescing.** The spec says "prioritize/coalesce renew/cancel/detach so polling cannot starve lifecycle handling" but gives no mechanism. `ControlGate` never lets status/result lookups take the last of the 8 slots. An identical lifecycle call (same kind, same session) already in flight shares its slot, and anything beyond the bound returns `busy`. Revisit this if the adapter needs a queue rather than an immediate `busy`, or needs more than one reserved lifecycle slot.
2. **Envelope errors.** A malformed `request_id`, an `operation_ref.session_id` that does not match the caller, or an unregistered target returns a non-frozen `invalid_request` code before admission, so no ID or slot is consumed. Ticket 14/15 maps it onto the carrier.
3. **Cross-session cancel/lookup.** For now these return `forbidden`, because ticket 10 owns cross-session history lookup. Cancelling another session's work stays forbidden permanently (spec §9).
4. **Status storage.** One record is reserved per admission, 1,024 records by default. Admission evicts the oldest terminal record that holds no slot. If every record belongs to active work, admission returns `busy`. Ticket 10 replaces this with byte-accounted retention.
5. **Withdrawn permission at the safe point.** Queued work whose capability was withdrawn becomes terminal `forbidden` without running. Work cancelled because its session ended becomes `cancelled`.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.
