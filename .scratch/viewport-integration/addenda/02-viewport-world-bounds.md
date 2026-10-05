# 02 — Usable Scene query from the viewport's ANARI world

Additive bounds delivery contract. User explicitly selected viewport-world bounds rather than certification of every current VSR Scene edit, after directing reuse of Frame All's existing world property. Original specs/historical tickets remain immutable. This is a scene-only provider increment, not the earlier complete CPU analytical/subtree bounds contract.

## Source and semantics

`vsr_bounds(request_id, scene_id)` can return the axis-aligned world box from an explicitly registered live viewport source. Reuse the existing ANARI world and its `"bounds"` property, `ANARI_FLOAT32_BOX3`, as Frame All does. No geometry scanner, render/camera reset, device creation/substitution, index population or Scene edit is introduced by inspection.

The box describes that ANARI WORLD, including its index's filtering and external instances, NOT a certified union of every current VSR Scene object. Report `coverage: viewport_world`, source kind, actual device/subtype and bounded source identity/generation. Current Scene/inspection revision tokens are observation provenance only; a covered Scene revision is unknown unless independently proved. Explicitly declare that distinction, never stamp the box with a fabricated covered-current revision or claim default-visible complete Scene coverage.

Choose the host's primary live interactive viewport deterministically for the registered Scene; document/report that selection. Do not silently use independent AgentView's world, create a fallback device or follow a retired source. Multiple-source behavior, source replacement and missing/inactive viewport results are explicit and lifetime-safe. Scene read authority remains the query requirement; do not expose protected view names/camera/image data through source metadata. Host callback invocation and publication both obey current Scene read authority.

## Results

Preserve `available`, `empty`, `pending`, `unavailable` state vocabulary. Available has independently correct finite ordered world min/max. Missing host binding, failed/unsupported property query, invalid/nonfinite box and retired/inactive source have distinct bounded reasons. Successful malformed output is NOT automatically evidence of emptiness. Empty requires an independently justified empty-world convention/coverage on the qualified provider; omit the box. Pending means an explicit transient host source/readiness state, not a fabricated geometry scan.

Unbound/custom hosts retain truthful missing_binding. `node_handle` and `include_disabled:true` remain unsupported before admission; omission/explicit false retain original request identity rules but no selection claims beyond source-world semantics. Report no CPU scans or geometry/vertex counters for this value/provider query.

## Query, timing and ownership

Add/reuse a status-preserving shared world-property helper. Existing Frame All helper/UI fallback behavior remains unchanged; MCP cannot use its synthetic unit-box fallback as real bounds. Keep the real API wait semantics explicit. `ANARI_WAIT` is not interruptible by an operation deadline, and changing to NO_WAIT does not prove correctness/boundedness. Qualify actual query latency/lifecycle on existing current barney and Helide worlds, including after normal UI/agent updates, without stalled host/UI service, forced rendering or source accumulation changes. Unsupported/unqualified configurations stay unavailable. If a driver call cannot meet a safe finite servicing story, stop and report rather than claim qualification from a flag/timer.

Preserve five-second admission-to-completion deadline, bounded metadata and shared8MiB inspection scratch, existing request admission/replay/conflicts, cancellation, authority suppression and safe source lifetime. Do not introduce an unbounded cache/result store. Historical replies retain their original box/source/revisions; retrieval does not silently re-query.

## Acceptance seam

Public headless real-Scene/provider and admitted MCP tests; real ANARI Helide/barney worlds with independently expected literal boxes and edit-update/query results; thin actual production viewer read-only query and backend timing. Include missing source, property failure, invalid/nonfinite output, empty/pending if provider supports them, cancellation, queue deadline, source retirement/replacement, read withdrawal before/after provider invocation, replay and scratch exhaustion. No source certification/CPU scanner/subtree/GPU-download/capture qualification is inferred.
