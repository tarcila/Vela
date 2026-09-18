# Addendum 03 — Envelope codes, operation policies and sync/restore/render details

**Extends:** spec §5, §9, §10, §11 and §12. The spec freezes named outcomes but leaves these cases open. **Source:** holes 37, 61, 62, 67, 73, 79, 91, 97. **Decided by:** orchestrator, ratifying the implementers' assumptions except where noted.

## Decisions
1. **`invalid_request` (37.2, 62.2–3, 91.2):** a non-frozen envelope code for malformed or mismatched request arguments, unregistered targets, an empty, oversized or duplicate-entry batch, invalid view dimensions, and invalid member values. It is rejected before admission, and consumes no ID, whenever it can be detected before admission. It is added to the documented code set alongside the frozen codes. It is not a frozen name, and clients must not treat it as authentication failure.
2. **Control path (37.1):** status and result lookups may never take the last control slot. Identical in-flight lifecycle calls (renew, cancel or detach from the same session) share one slot. Excess calls return `busy`.
3. **Withdrawn permission at the safe point (37.5):** the work ends `forbidden`. Work cancelled because its session ended ends `cancelled`.
4. **Preparation or allocation failure (62.1, 73.1):** the result is terminal `busy` with `committed=false` and unchanged eligibility.
5. **Progress while the hook runs (61):** a committed operation whose normal-synchronization hook is executing reports `synchronizing`, not `running`. This corrects ticket 07's behavior.
6. **Resynchronize outcome (67.1):** a Resynchronize whose recovery ran but failed ends with terminal `ok` and result `{ready:false, state:"failed"}`. A successful one ends `ok` with `{ready:true, state:"ready"}`. The operation succeeded as an attempt; readiness is the payload.
7. **View registration after activation (67.2–3):** registering a view after activation is host work at a safe point and uses no integration slot. The host chooses fail versus `needs_sync` for each registration.
8. **`needs_sync` precedence (67.4–5):** `needs_sync` behaves like `failed` for ordinary edits. Only Resynchronize clears it. Reported state precedence: recovery in progress, then failed, then needs_sync, then pending hooks, then ready.
9. **Restore details (73.2, 73.5):** a missing affected object or parameter returns `conflict`. A batch named on another scene returns `outcome_unavailable`. Local restores do not replay; the first commit wins and the second gets `already_restored`.
10. **Activity accounting (79):** each entry costs 256 bytes plus its payload bytes, and images count only against the image budget. When the image budget is full, the image is evicted but its metadata is kept. An expired batch keeps its metadata until the entry itself is evicted. Another session's unknown reference returns `outcome_unavailable`. Your own record with partial read returns only the minimal receipt.
11. **Render concurrency (91.1, 91.3):** each view has one frame in flight. While any render holds the view, new renders of it return `busy` before admission. Outcomes, including success, are reported only after the backend's resources are safely released.
12. **Carrier rejections (97):** a Host mismatch, a missing or duplicate Host, or any forwarding header returns HTTP 400 with `data.code="invalid_host"`. Origin present returns HTTP 403 with `data.code="forbidden_origin"`. Neither is a frozen name.

## Why
Each case needs a stable machine-readable answer for the headless API and the adapter mapping, and the frozen set intentionally does not cover them. The chosen answers were already implemented and tested. Item 5 is the only behavior change.
