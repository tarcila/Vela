# 91 — Render: one in-flight frame per view and invalid-dimension code

**Status:** resolved
**Milestone:** 1
**Kind:** extends
**Blocked by:** none
**Files:** src/vsr/integration/render/RevisionRender.hpp

Assumptions made by ticket 12 (`render::RenderService`), cheap to reverse:

1. **One frame per view.** A view's `RenderBackend` owns a single frame. While any render (live or abandoned-awaiting-cleanup) holds it, new renders of that view reject `busy` before admission; a render that reaches the safe point while another holds the view ends `busy` without backend work. Spec §5 only mandates busy for resources held by logically cancelled/timed-out work; it is silent on queueing behind a live render. Revisit if clients should queue multiple renders per view (needs per-request backend resources or a wait inside the slot).
2. **Invalid/oversized view dimensions** reject pre-admission with `invalid_request` (the engine's envelope code), no ID consumed. Spec §12 says "reject before backend rendering" without a code; a view whose dimensions become invalid after admission ends `invalid_request` at the safe point. Revisit if the adapter needs a distinct code.
3. **Guard-terminated renders whose release is not yet safe** stay `running` (BACKEND_PENDING) until release, then report the guard outcome; success likewise is reported only after safe release.
4. The completed image is appended directly to `activity::ActivityLog` (image budget) by the render service; ticket 80 generalises engine→log wiring.

### 2026-09-28 orchestrator
Resolved by addendum 03 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 139.
