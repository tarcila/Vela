# Presented CPU Frame Observation

The synchronous **Linux CPU** path is qualified through real Scene/Helide
ImagePipeline production, actual SDL3 display submissions and admitted MCP
retrieval. Fixed infrastructure, guarded encoder stack, bookkeeping, snapshots,
encoding and retained/transport results share one finite ownership budget.
Exposure still defaults false; no existing viewer source is enabled. Each host
must qualify its actual binding/display callback before opting in. Other worker
platforms remain unexposed; current viewer/barney/CUDA pixels require ticket04.
CPU qualification is not whole-spec completion or GPU/monitor parity.

`FrameService` is the public headless host adapter for **qualified synchronous
CPU** Image Pipeline output. It does not render in response to a request. Use
one service/shared `rendering::FrameBudget` for all of a host's observed views.
Rendering-local snapshots contain no Scene, UI or integration wire pointers.

## Host contract

1. Register the real view binding and distinct Scene/View revision trackers.
   Attach the view to `FrameService`; exposure initially remains false. Roles
   and read grants alone never enable production. Register `addFrameTools()`
   with the normal MCP server/OperationEngine, without changing authentication.
2. Qualify the producer: final RGBA8 sRGB pipeline output must be CPU resident,
   fully completed and synchronous, with truthful actual camera/renderer,
   device, dimensions, display scale and pipeline settings. Include in-pipeline
   Outlines, not UI/HUD/gizmos. An asynchronously completed ANARI frame cannot
   be stamped with the latest UI configuration. The existing CUDA/default
   pipeline is **not** qualified by this adapter or by these CPU tests.
3. After CPU qualification, call `setExposed(view, true)` on the host thread.
   It publishes the finite observation declaration; fresh capture stays closed.
   At least one currently authorized admitted frame request activates the
   observer. The SAME first operation stays pending during genuine cold
   preparation, awaiting natural successful presentation within its original
   admission deadline and policy lifetime. It never issues a second request
   or forces rendering. Unsupported/acquisition/resource failures remain
   meaningful terminal failures, not an activation retry.
4. At an owning-thread Integration safe point, call
   `beginProduction(view, pipeline, rowOrigin, alphaMode)` **before** the
   natural producer executes `pipeline.render()`. Move that receipt to
   `present(view, pipeline, receipt, submit)` after rendering. `submit` must
   perform the actual viewport display submission and return true only on its
   success (e.g. SDL RenderTexture plus successful RenderPresent). Texture
   upload alone is not success. The callback must not reuse/re-render/resize the
   source before acquisition. Detected generation/size reuse returns unavailable
   for that latest presentation, never a previous completed image.
5. Call `pump()` at each owning-thread safe point, including while no requests
   arrive. It reclaims withdrawn/expired/closed sources and results and retries
   a completion wake that races OperationEngine's backend-pending transition.
   Pending activation lives in the existing bounded Job pool, not another
   queue. Pump wakes it only on readiness, a meaningful failure, authority or
   lifetime withdrawal, or source identity change; it does not self-wake while
   awaiting presentation. The engine's bounded abandonment query detects
   cancellation/deadline without retaining credentials or polling status.
   A backend wake retains the original Scene safe-point route for cleanup
   after view closure, never a replacement view. Continue normal rendering
   while the bounded CPU encoder worker runs.
6. Before any device/source replacement, withdraw exposure with
   `setExposed(view, false)`. Requalification is cold even if the new Device
   Identifier string equals the previous one. Device-Identifier transitions
   are additionally detected at observation. Retire/recreate registered views
   rather than reuse identities. Stop the MCP server before destroying its
   tool dependencies; the engine/Integration outlive the service and draining
   callbacks. Service destruction joins only bounded CPU encoding, no GPU wait.

The snapshot is copied only after actual successful submission and a repeated
observer-authority check. A warm request pins at its execution safe point.
Cold preparation retains the source attachment generation, retirement epoch,
Device Identifier/subtype and storage-instance generation, but pins no frame.
After readiness, the later owning-thread observation safe point pins latest
exactly once with its matched receipt and actual observation/submission times.
Source changes cannot migrate a pending activation; cancellation/withdrawal
cannot resurrect its observer. Other authorized observers remain active.
Deadlines, policy lifetimes and capacities are not extended by waking. Encoding reads only this immutable owned
snapshot on a worker; publication rechecks current Scene/view read rights.
Current revisions are those observed at this point, not delivery arrival.
Retained replay keeps the same historical image/metadata after resize or later
presentation. Read withdrawal yields only the owner's minimal operation receipt.
Images/results have a positive policy lifetime (at most120 seconds), at most64
retained operation-image entries and one concurrent encoder. Cancellation keeps
its operation slot until owned encoding has drained; it cannot publish an image.

## Camera receipts and freshness

A `vsr_set_view_camera` receipt describes current effective state, not pixels.
The existing `vsr_view_frame` terminal result carries `state`, `image_kind`,
`view_id`, `view_identity`, `rendered_view_revision`, `rendered_scene_revision`,
`current_view_revision`, `current_scene_revision`, `stale`,
`rendered_view_revision_basis`, and `rendered_configuration.camera`.
For qualified viewer evidence require `state="available"`,
`image_kind="presented_frame"`, the addressed identity, the camera receipt's
exact rendered view revision and matching world-space camera/FOV, the chosen
post-move Scene revision, `rendered_view_revision_basis="anari_render_start"`,
and `stale=false`. Presentation time alone proves none of these.

Old presentations can remain available while rendering catches up. Poll with
fresh request IDs for at most 120 seconds, then report timeout. Historical replay
is not fresh proof. User navigation or a later camera move supersedes the prior
receipt; obtain a new target rather than accept an unrelated newer revision.
Unsupported/unqualified binding evidence remains unavailable/incomplete.
Camera control does not widen qualification. Independent rendering and effective
view-info cannot substitute for the explicitly addressed presented frame.

## Bounds and image semantics

Limits can narrow the existing2048-axis/2073600-pixel/8MiB PNG/12MiB response
ceilings; the16MiB proxy ceiling is unchanged. No hidden resampling occurs.
Oversize states carry actual submitted native dimensions and effective limits.
The viewer capture is the RGBA8 sRGB buffer uploaded for display, after every
in-pipeline effect (AOV, tone mapping operator, exposure/auto exposure, gamma,
Outlines), so any such configuration is captured as presented and reported
from that upload. Only an unknown pipeline source or pixel encoding is
`unsupported_output_profile`.
`pngRgba8OutputBound()` is the authoritative proved encoder-output bound.

One finite shared reservation pool (validated, at most256MiB) accounts for the
budget object and every lease/control allocation, service state, per-view source
and binding bookkeeping, per-operation jobs/map nodes/executor copies/futures,
source snapshots, held old snapshots, matched receipts, PNGs and operation
metadata, encoding scratch and transport serialization/drain. Infrastructure
must fit before sources attach. Linux uses an explicitly mapped2MiB encoder
stack and64KiB PROT_NONE guard; pthreads use that exact user-owned stack, which
is unmapped after join rather than retained in libc's default-stack cache.
Additional fixed allowances cover thread/TLS/tcache bookkeeping and the bounded
pump wake vector. Cache replacement/expiry never releases a reservation still
owned by a consumer. Allocation failure leaves counters unchanged and unwinds
leases at the application boundary; a failed copy never revives an older frame.
Checked source dimensions bound all later products and sums. Encoder scratch
reserves16 filtered-array sizes plus8MiB: this covers normalization, filtering,
stb's proved output/capacity/reallocation bounds, fixed-quality hash lists,
PNG/fallback overlap and bookkeeping. PNG retention is separately reserved at
its proved upper bound. Delivery reserves eight complete-response bounds plus
4096 bytes before serialization, and transfers its lease from ToolResult to
HttpResponse until send completion/timeout. Visible public Status copies,
including generic lookup/cancel and replay, reserve before copying protected
metadata and retain that lease through serialization. Bounded frame responses
check the complete envelope and allow at most1024 serialized RPC-ID bytes;
oversized envelopes return busy without protected payload. Read withdrawal
reclaims unheld frame results while in-flight, already-published socket drains
keep their immutable leases. Busy/exhaustion does not reset, resize, cancel or
replace the natural viewport producer.

PNG normalization uses the existing image primitives exactly once: top-left
straight-alpha sRGB RGBA8 output; declared bottom-left/premultiplied CPU sources
are normalized, not tone mapped again. Monitor/compositor color management is
outside the contract. Rendering/Scene/device allocations, the existing generic
MCP server infrastructure, shared allocator unused arenas/free caches and copies
made by clients outside this ownership API are not observation-owned budget
claims. Live allocation headers/page rounding and observation-created worker
stack/control/cache allowances are included; this is not a process-RSS/virtual
address-space ceiling. The qualified binding callback must itself return the
bounded host-owned metadata contract, not retain arbitrarily oversized vector
capacities or allocate unbounded host-side side effects.

The real headless tests use a Scene/RenderIndex/Helide natural producer, CPU
ImagePipeline storage with CUDA still compiled on, and actual SDL3 software
display submissions. A procedural final pass supplies an independent asymmetric
pixel oracle; another case disables it and compares real Scene pipeline output
against the submitted SDL target. Loopback MCP checks exercise admitted tools,
read withdrawal and legacy isolation. Additional tests cover two authorized
real CPU producers near the256MiB shared ceiling, genuine same-Identifier CPU
device replacement, high-entropy maximum-size PNGs, multiple actual slow HTTP
socket drains, response exhaustion, cancellation and read withdrawal while
normal production continues. Valgrind, sampled Massif attribution and stack
mmap/guard/munmap syscall evidence supplement the allocation/lifetime bounds;
reservation counters alone are not a memory proof. This is not current viewer/barney/CUDA,
physical-monitor, UI-overlay or GPU-latency qualification; those require the
separate viewer adapter and evidence.
