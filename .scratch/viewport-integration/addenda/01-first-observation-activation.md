# 01 — Complete the first observation after cold activation

Additive contract for the frozen Truthful Views, Presented Frames and Safe Selection Translation specification. Authorized by the user's instruction to fix first observation before bounds, generic editing and image delivery. The original spec remains unchanged.

## Cold activation

For an authorized, exposed, qualified view, the first admitted `vsr_view_frame` operation must not terminate with `unavailable/cold_cache` merely because it activated observation. If no observation-owned snapshot exists, activate the observer and keep that SAME operation pending until a natural successful presentation can be observed within its existing admission-to-completion deadline.

Cold activation is preparation, not the frame observation point. The owning-thread observation point is the later safe point at which this operation first pins the latest safely acquirable, actually presented snapshot and matched receipt. Report its actual observation/submission identities, monotonic times and matched rendered/current revisions. Never claim those are admission-time values. No subsequent presentation can replace a pinned identity while encoding/delivery/replay continues.

A warm request continues to pin latest at its ordinary execution safe point. Do not wait for a newer frame or an available earlier readback when that request already identifies a specific latest presentation. Only snapshot absence caused by genuine cold activation invokes this preparation wait; oversize, unsupported profiles, lost acquisition, resource exhaustion, unavailable bindings and other meaningful failures retain their truthful behavior.

## Safety and lifecycle

No forced rendering, resets, accumulation changes, camera/device mutations, GPU readback fallback or extra client request. Observation work remains qualified and authorized; no permanent cache is enabled merely by registering a view or discovering it. Existing source qualification, shared budgets, single encoder, bounded job/result capacity, positive lifetimes and response/image caps remain unchanged.

Pending activation must respect cancellation, session/read withdrawal, deadline/no natural presentation, view removal/close/recreate, source withdrawal and source-instance/device changes. Do not silently follow a replacement source or revive a retired observer from a cancelled/expired operation. Wake waiting work through the owning-thread pump/presentation lifecycle without busy-spin or an unbounded extra queue; account for wake races and retained callbacks.

## Evidence

Use the already-approved public real-Scene/SDL FrameService and admitted MCP seams, supplemented by actual production barney/Helide display-to-PNG comparison. Demonstrate RED first, then same-operation first-image success with no second frame request. Preserve warm exact-latest pinning, historical replay, authority suppression, cancellation, source lifecycle, bounded ownership and slow-drain tests. Update explicit old cold-cache tests only where this additive contract changes their expectation; keep their original safety assertions.
