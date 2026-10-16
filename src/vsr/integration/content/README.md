# HDRI Content Creation — registered, pending central review

`ContentCreation` and its strict MCP adapter are now registered by `vsrViewer`
(with `--agentIntegration`) and by `vsrTutorialIntegrationHeadless`, together
with the trusted host asset-root startup controls described below. Only the
actually implemented safe Radiance RGBE HDRI profile is advertised. No Surface,
Volume, Asset Import, generic constructor, root-grant tool or new Undo store is
provided by this slice. Independent Standards/Spec review and integration
landing remain outstanding; registration is not a review sign-off.

## Trusted host asset roots (startup controls, never agent grants)

Read authority is a **host** control with exactly two sources, both resolved at
launch, both outside any agent's reach:

- `--agentAssetRoot <absolute-directory>`, repeatable up to 8 times, no
  duplicates, no `.`/`..`/empty components, no trailing slash, no `=` form.
  These are ordinary operator folders: their mode and ownership are read and
  **never changed**, and they are explicitly **not** required to be `0700`.
  A refused directory is reported and fails the launch rather than silently
  dropping authority.
- One launch-private working input namespace, created mode `0700` beneath the
  validated per-user runtime directory as `content-<launch id>` and pinned for
  the launch. Only this namespace must be private; it is validated for `0700`
  and effective-user ownership by both `openDirectory` and `approve()`. At
  teardown the launch removes it only when empty — operator files left there
  are never deleted.

`vsr_content_options` publishes `asset_root_profiles`: the root ids, their kind
(`host_startup` / `launch_private`), a bounded host label and approval state.
**No path, credential or grant is ever published**, and no MCP tool, request
field or environment variable can add, rename or widen a root. An agent may
only select an existing root id plus a relative file name, which the kernel
resolves beneath that pinned directory fd.

`scripts/test-content-host-roots.py` (ctest `vsr::content-host-roots`)
qualifies this against the **installed** headless CLI: refused malformed and
unsafe options, discovery without paths, traversal/symlink/absolute refusal,
unapproved root ids, creation and ordinary Restore from both an ordinary 0755
operator folder and the private namespace, and an unchanged operator folder
mode across the whole run.

## Operator/tool contract

A trusted custom host supplies the shared physical-memory reservation provider,
EditService, OperationEngine and ActivityLog, then attaches its qualified Scene
identity. Before admitting work, the host calls `assetRoots().approve()` on
absolute asset directories. At most 16 roots are allowed; duplicate directories,
symlinks and unconfined paths are rejected. Shared asset directories need not be
`0700`; approval does not chmod them. `revoke(id)` withdraws access, including
from already prepared snapshots. These are direct trusted host controls, **not
agent permissions**. Hosts that embed the services directly use the same
`configureHostAssetRoots()` wiring as the viewer and headless application.

`vsr_content_options` requires Scene READ and
reports the actual profile/defaults/limits. `vsr_create_hdri` requires
Scene READ+EDIT and enabled agent edits. Required fields:

- `request_id`, `scene_id`, host-issued `asset_root` (0–15), relative `file_name`.
- Optional `root_name` (default `HDRI Content`), Layer/parent handles, or an
  explicitly `new_layer` (mutually exclusive with existing destinations).
- Typed `scale` (default 1), `up` (default `[0,1,0]`), `direction` (default
  `[1,0,0]`), and 16-number column-major affine `transform` (identity default).
  HDR values are not premultiplied by scale. No implicit normalization, camera
  framing, lighting replacement, filename/name deduplication or save occurs.
- `approval_request_id` is the existing trusted, bounded, one-operation recovery
  approval mechanism. It cannot grant filesystem authority or permanently raise
  the hard cap. Unknown fields and non-finite typed values are rejected.

Poll `vsr_operation_status` with the admitted operation reference. Committed
results contain the primary Light/radiance/Layer/Content Root/placement handles,
effective settings, dimensions, root label, ownership and normal `batch_ref`.
Use existing inspection/render tools and ordinary `vsr_restore`; no alternative
creation-specific removal command exists. Replay returns the original outcome,
not another graph, another recovery record or a refreshed FIFO age.

## Source profile and bounds

Only real Radiance RGBE `-Y +X`, flat or modern scanline RLE, is supported.
Other orientations, old repeat-marker RGBE, EXR/PNG and malformed/truncated
sources are rejected, not normalized or replaced by an 8-bit image. Linear
FLOAT32 RGB rows are bottom-up, as required by HDRI Light radiance and ADR 0014;
Sampler Image orientation is a different contract.

Operation limits: 4 MiB input, 1 Mi pixels, 4096 per dimension,
a 32 MiB worker backing arena plus an allocation-free graph backing profile
capped at 64 MiB per job, 16 KiB result, 1024 ancestors and a 30-second logical
deadline. These are separate from the existing shared host limit (not an
increase to its default). The advertised 164 MiB processing envelope comprises the 32 MiB worker,
64 MiB initial-admission, 64 MiB graph and 4 MiB Token profile ceilings;
these independently held phases can overlap. It is not a measured peak.
 Job/host/result/recovery controls and the physical
provider's lease bookkeeping are additional shared charges. The creator has at most 32 live Jobs and 32 attached
Scene registrations; expired weak Job entries are pruned even on failed/replayed
admission. Root/path bounds are 16/4096. Removal scans at most 32768 Scene
pool/Layer/Node slots (holes included) and 131072 values/elements. Admission
leaves room for the new closure within those scan bounds. Scan exhaustion
refuses publication/removal; it never authorizes force deletion.

The shared physical provider gates job/control/result/recovery reservations.
The creator's State allocation/control block uses CapacityAllocator, and its
32-entry jobs/source-map backing is reserved using container element/node sizes.
Root-vector and Root/control allocations use a separate 1 MiB arena. Job and
HDRI Snapshot coallocations use CapacityAllocator through last-weak deallocation.
Job holds one request/settings owner, not request copies in executor and worker
captures. Before construction, a checked 1 MiB control profile covers retained
input capacity, eight simultaneous canonical/receipt construction buffers (sixfold
JSON escaping and libstdc++ doubling), and at most 32 internal callback controls.
The largest internal guard has two bounded key/session strings and four shared
pointer-sized captures; externally installed opaque providers are not cloned.
The async state/control/result sizes come from the actual libstdc++ instantiation;
its thread trampoline is bounded by its two members, virtual base and alignment.
OS thread/runtime overhead is not a total-process memory guarantee.

The two retained publication strings (Engine fallback/result and Activity's
reserved receipt) enter policy admission as distinct owners. Their physical
leases already belong to their producers, not a duplicate Snapshot allocation.
Status/Activity/Restore copies have independent two-times-16-KiB string/control
coverage; HDRI MCP carriers separately profile actual escaping and overlapping
RPC/HTTP encoding before constructing strings. McpServer embeds the two carrier
strings directly; it does not allocate a second result parser tree.
Copied carriers acquire new coverage. Queued dispatch and shared wake/cancellation
controls retain their original lease through the last capture owner, independently
of Job completion/record expiry. Job completion does not claim those bytes freed.
These bounds do not claim total application/GPU memory control or successful
allocation.

Worker source bytes, decoded FLOAT32 RGB and RGBE row buffers now use the same
physical provider at the actual allocator boundary. Allocation requests include
capacity, an in-allocation ownership header, and max-align rounding, with checked
size arithmetic. Arena admission precedes allocation. Growing a buffer holds
both old and new backing charges until physical deallocation; `clear()` does not
release capacity. Copies reserve their own physical backing, and moving/swapping
or releasing a copy off-thread does not lose its charge. The finite 32 MiB arena
bounds simultaneous worker backing, not Undo retention. Successful
receipts report `worker_allocator_peak_bytes`, measured from actual live backing
allocations and distinct from required recovery capacity. This is specifically a
worker statistic, **not a claim to have measured total publication or GPU peak**.
Worker allocation denial reports noncommitting `busy/worker_backing_limit` and
preserves prior recovery history.

The RGBE payload bounds are engineered from at most 4 MiB source, 12 MiB decoded
radiance and one at-most-16 KiB row, plus each actual header/alignment charge;
32 MiB also leaves room for overlapping reallocations. Decoder working validation
separately allows a future 12 MiB detached Array. Header and resolution parsing
now uses allocation-free string views/from_chars, not streams or heap strings. The provisional 128 MiB publication estimate is removed. Before
constructing detached content, the owner profiles actual value/marker/free-index
backing, destination Forest storage, LayerMap copy/growth overlap, delegate and
ancestry vectors, and identity registry nodes/buckets/rehashes. Checked arithmetic
rejects over-capacity backing even when logical slot counts are small. Relevant
Object/Node metadata has a 64 KiB per-owner bound, 64 Parameters/instance values,
and no supported DataTree snapshot profile; unsupported existing DataTree metadata
in the affected Object pools fails closed. Registry profiles require load factor
1 and at most 65536 buckets, with at most 32768 registry entries. Only libstdc++
vector reserve/doubling, bitmap word rounding, prime rehash and node layouts are
qualified; other implementations refuse instead of assuming identical growth.

`preparation_peak_bound_bytes` is a conservative requested graph-backing bound,
not allocator-observed total application peak. It includes old/new reserve overlap
before growth. `live_payload_adopted_bytes` identifies the detached radiance bytes
transferred into the live Scene, and `live_scene_backing_after_prepare_bound_bytes`
describes now-live reserve backing, not physical reclamation. A distinct
PreparationOwnership reservation survives private plan destruction. Published
Array/pool/Forest/registry backing is live Scene ownership, not an automatic undo
payload or permanent FrameBudget charge. Abort can leave live reserve growth; its
release is likewise not a claim those physical bytes were freed. The literal
worker CapacityAllocator contract is unchanged.

Temporary removal coverage now uses the checked graph/tombstone/radiance profile,
not an 8 KiB estimate. The lease precedes the transaction in destruction order,
so it survives notification exceptions and radiance tombstone destruction.
Before initial lazy identity lookup or ancestry-vector allocation, a bounded
allocation-free destination/registry profile now reserves coverage. Oversized
existing destination metadata fails before source I/O. Source Snapshot/control
allocation uses CapacityAllocator through the last weak owner; separately
reserved Currency/path backing survives until destruction. Bounded new-Layer
Token interning reserves node/string/rehash overlap under the registry mutex.
That storage is adopted by the permanent registry, not reclaimed with the Job.
HDRI RestorationRecord copies independently reserve their record/string backing
before copy; assignment keeps old coverage until replaced storage is destroyed.
ContentCreation now accepts a `Reservation`: an already owned host service and
noncapturing dispatch pointer, never an arbitrary copied std::function capture.
The service's original backing remains the host's lifetime responsibility;
Reservation copies allocate nothing and retain that same authority. Bounded HDRI
result factories bind this authority and a byte count inline. ResultOwnership
copies share the immutable factory but acquire independent payload leases before
copying strings. Legacy non-HDRI factories remain supported without claiming
that their initial arbitrary captures have a capacity profile. The isolated
fault-injection fixture moves its existing provider into an external service;
that adapter is not a production capture-capacity query.

Creator source-map nodes/jobs-vector backing and CapacityArena coallocated root
controls now have literal CapacityAllocator ownership, including final weak,
off-thread control deallocation. There is no fixed 32-source map-node allowance.
Worker observers also use a shared host service/noncapturing dispatch pair;
workers do not clone targets. HDRI scope admission uses the actual libstdc++
record/scope tree-node types and bounded key/order/expired-ID strings. One map node
transfers from pending to live; no new publication allocation is hidden. Scope
leases move to expired metadata and remain owned independently of eligibility.
The expired vector already has 1024 preallocated slots and ages by publication;
it is metadata history, not an unbounded journal or an undo clock.

The Store reserves declared 4096-record/1024-scope order capacities plus its
bounded pending capacity before the first HDRI publication. Its own lease covers
persistent backing through Store destruction, including old/new reserve overlap
and partial reserve failure. This deliberately overcharges existing order
capacity rather than pretending to instrument baseline Store/Activity allocation;
there is no per-record duplicate full-vector reserve. Later ordinary history
cannot grow beyond the qualified backing without a prewrite refusal. Metadata
Snapshots remain weak and contain no radiance bytes. Startup/private-root wiring,
production registration and central review remain required; the bounded internal
control profile is not a global malloc/Scene/GPU accounting promise.

## Confinement, worker currency and lifecycle

Actual opens require Linux `openat2` with `RESOLVE_BENEATH`,
`RESOLVE_NO_SYMLINKS` and `RESOLVE_NO_MAGICLINKS`; unsupported platforms do not
fall back to weaker resolution. Only regular files are read. No absolute path,
traversal/dot component, symlink escape, hidden network fetch or recursive copy
is permitted. Directory fds pin approved directories without broadening access.

The worker owns an immutable byte snapshot and source fd. Reading, decoding and
consumed-byte comparison run off the Scene thread. Currency compares identity/size/mtime/ctime and reads actual consumed bytes
through **both** the original held source fd and a freshly confined named fd,
followed by another metadata check of both. These full comparisons remain on
the worker; the final owning-thread guard performs no payload reads. The owner repeats bounded confined fd/stat
checks, root authority, destination ancestry history and current READ/EDIT
immediately before coherent publication. Unrelated camera/borrowed-label edits
are not global-revision conflicts.

Cancellation is cooperative at I/O chunks/decode rows, not a forced kernel-read
interrupt. Logical cancellation/deadline/session end may leave
`awaiting_cleanup`: the slot and physical work leases remain held until the
worker exits. `pump()` handles completion wakes at owner safe points. Host
teardown cancels and joins workers. A trusted idle-installed worker observer can
report a checkpoint; it runs off-thread and must not access the live Scene.
Tests hold that checkpoint to prove real cancellation/deadline/teardown ownership,
not to bypass current source or authority guards.

## Publication and conditional removal

Detached graph and Object/Layer/Node registry preparation, receipt/fallback,
Store/Activity reservations and final guards precede the first write. Commit
adopts a complete Light/radiance/root/placement and optional Layer without
callbacks or allocation. All primary handles resolve before notification.
Normal batched fanout follows Store/Activity publication. Notification/hook
failure remains truthfully committed with the promised batch retained; it does
not automatically roll back or masquerade as a nonmutating failure.

Removal evidence is weak and metadata-only. It checks owned identity,
parameter/metadata/data write histories, exact membership, local transform and
label/instance/default history, safe optional owned Layer and authoritative
outside-use evidence. Ordinary equal-byte radiance writes and change-back edits
do not revive eligibility. Borrowed parent/Layer labels and unrelated camera
changes alone do not invalidate removal. Existing transform-only Restore remains
narrow and separate. Ordinary owned Parameter description, bounds, usage,
enumeration/selection and enabled-state setters spend the owning Object's local
metadata history even on an equal write or change-back, without relying on a
Scene callback. Scalar value-only recovery does not acquire a metadata guard.
Parameter copies and move construction are detached snapshots; moving from a
live Parameter leaves that slot unchanged. Assignment preserves the target
name/owner, publishes copied metadata/value and spends its histories. Object
pool/map relocation rebinds setters without rewinding the logical history;
weak owner state cannot call a destroyed Object. Whole-map prepared writes also
reject intervening metadata change-back before swapping a map.

Use counts cover ordinary app/animation/internal pins. The bounded authoritative
scan additionally covers every Object's current/disabled parameter values and
min/max values, typed object-array elements, Node current/default values and
instance parameters, even bindings deliberately omitting counts. Unreadable or
mapped object arrays conservatively prevent removal. Arbitrary memory corruption
or externally fabricated native handles are not supported reference forms.

Prepared removal preallocates tombstones and unregisters identities before
notifications. Temporary deleted payload ownership has a separate processing
lease, not an uncharged undo pin. Eviction expires eligibility without deleting
live content. Ordinary Restore removes the complete unchanged owned graph;
edited/new-child/reparented/unsafe outside-use cases conflict without writes.

## Render verification and still-open delivery gates

Ticket 01 now uses the explicitly authorized HDRI-capable GPU control. Its
public creation/status/inspection/Render-image/ordinary Restore tracer uses one
bound Device and RenderIndex throughout, with no rebind/repopulation workaround.
The fixture's actual `(8,4,2)` RGBE source and matte 0.5 remain unchanged. Explicit
`Light.scale=0.0625` keeps HDR-coloured geometry below 8-bit saturation, while
requiring more than 30 geometry-region pixels brighter than the baseline and
redder than blue. A preexisting directional Light and camera remain untouched;
removal restores exact baseline pixels. VisRTX explicitly uses advertised
`quality`; Barney uses advertised `default`. Runtime plugin paths/settings/PNGs
are recorded by the qualification host. These are independent renders, not
presented viewport captures or inherited native Mesh evidence.

The original installed Helide Light-factory failure and red evidence remain in
the ticket's diagnostic artifacts. Helide is still a supported geometry/data
fast control, **not** an HDRI illumination pass. BOTH VisRTX and Barney remain
mandatory in tickets 08/09 for the complete later content milestone.

Ticket 01's accounting, startup/private-root controls and installed operator
discovery have landed. Independent central Standards/Spec review, default-`g`
integration verification and landing remain outstanding, as does the later
tickets' all-kinds device qualification. Never expose future formats or widen
this profile merely because its GPU tracer passes.
