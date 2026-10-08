# Export, Import and restore live Image PNG working copies

The qualified integration-enabled viewer and headless tutorial register
`vsr_image_export` and recoverable `vsr_image_import`. A successful export is a read, not an edit,
render, scene save, or original-asset write. It needs current Scene READ, even
when agent edits are disabled; it does not map the Array, reserve a write
sequence, change texels, or occupy a presentation/render encoder slot.

Discover the Scene and its `image_exchange` observation. On qualified Linux
hosts it reports `supported`, `enabled`, `export`, `import`,
`data_tracking`, `image_exchange_directory`, and `exchange_limits`.
Unsupported platforms or unsafe runtime directories fail closed. The strict
v1 connection descriptor has optional `image_exchange` launch metadata with
these directory and Image-independent limit fields; existing descriptors that
omit it remain readable by the proxy. Nested fields, types, duplicates, limits
and launch-directory shape are validated, not accepted as arbitrary JSON.
Authenticated discovery reports current availability: the descriptor is a
startup observation, not authority or a promise that the directory still exists.
No session-specific availability is included in the descriptor. Do not infer
exchange availability from the tracking prerequisite alone. Array `image_info`
reports the same unavailable reason after directory replacement or loss while
already-enrolled write history continues; recovery never reuses an old token.

Inspect the Image Array (not its Sampler) and copy its opaque handle:

```json
{"request_id":2,"scene_id":0,"handle":"<array.handle>"}
```

Call `vsr_image_export`, then poll its qualified operation reference with
`vsr_operation_status`. Admission, polling, historical replay, later-session
lookup and activity use kind `image_export`. Unknown arguments, including a
caller-chosen file name or path, reject before consuming the request ID. At the
owning-thread safe point the host rechecks authority, Scene ownership,
Array lifetime/storage, and actual outstanding mapping. A known other-Scene
Array returns `invalid_request/foreign_scene`; an open map returns
`busy/image_mapped` without closing it. Unsupported storage returns
`invalid_request/unsupported_image`.

The terminal successful `result` contains:

- `handle`, `file_name`, `data_version`, `dimensions: [width,height]`,
  `element_type`, `channel_count`, `color_space: linear|srgb`,
  `row_order: top_down`;
- `affected_samplers`, direct non-Sampler `affected_users`, and
  `affected_complete`. These are captured bindings, **not mutations**.
  Enumeration conservatively charges candidate slots and parameter visits
  against 4096 work units and returns at most 256 combined handles. Incomplete
  coverage does not fail export; use ordinary paginated inspection for more;
- `exchange_limits` as described below. `committed` remains false.

Metadata, bindings, completed data version and bytes are captured together
while unmapped on the Scene owner. A worker only receives immutable owned
bytes. Two exports may both succeed with distinct names; a host write between
captures legitimately gives different versions. A completed PNG is closed
before its receipt. A replay returns its original name/version even after file
editing or deletion: it neither regenerates nor attests to current file bytes.

## Raw sample policy

All supported signed/unsigned linear fixed8 and unsigned sRGB Image types
retain their raw bytes. Channel counts 1–4 become PNG grayscale,
grayscale-alpha, RGB and RGBA, respectively: color types 0, 4, 2 and 6 at bit
depth 8, compression/filter methods 0 and interlace 0. Rows stay top first;
straight alpha preserves nonzero transparent color bytes. No channel expansion,
gamma conversion, palette conversion or premultiplication occurs. Files contain
only IHDR, IDAT and IEND, never gAMA, sRGB, iCCP or cHRM. Color Space is reported
out of band. External tools should preserve raw samples, dimensions and channels.

## Bounds and encoder proof

Checked arithmetic computes `T = width * height * channels`, `F = T + height`,
`E = min(67108864, 2*F + 65536)` and `W = 4*F + 1048576`. Export reserves
`T + E + W` against the effective shared export transient ceiling **before**
copying. It has no undo snapshot. The ceiling is the minimum of 268435456,
`ExchangeLimits::transientBytes`, and the configured host `physicalByteCap`;
remaining shared capacity can further narrow admission. Pressure rejects
`busy/resource_limit`. Hosts supply `reservePhysicalMemory` to pre-acquire one
physical ownership lease; the viewer shares its finite `FrameBudget` with
presented-frame acquisition/encoding/transport, and the headless host also uses
a finite physical pool. The lease survives cancellation/backend cleanup until
all buffers, worker stack and control state are actually destroyed. No undo or
session charge is acquired. A lifetime-stable, thread-safe host PMR allocator
may control the capture/output allocations separately; it is not a substitute
for shared worst-case admission. Allocator and worker-start failures unwind
reservations and reject busy.

The export encoder is separate from the RGBA presentation encoder. It emits
one filter-0 byte per row directly from the capture into stored DEFLATE blocks
of at most 65535 bytes. With `B = ceil(F/65535)`, the exact PNG length is
`F + 5*B + 63`: 6 zlib framing bytes, 5 per block, and 57 PNG framing bytes.
It reserves that exact output once, emits no separate filtered buffer, and
uses constant codec state. W also covers a privately mapped 512 KiB worker
stack, 64 KiB inaccessible guard, and up to 448 KiB for thread/TLS, future/task,
quota-directory buffers, bounded binding/receipt strings and bookkeeping. No
libc default worker stack is used or left in its stack cache: join and munmap
precede physical-lease release. The codec itself allocates no work buffer;
its scalar state and 65536-byte write slices are inside these bounds. The two
PMR allocations are exactly T and the exact PNG length, never a growing codec
arena. Existing Scene storage and generic host/server infrastructure are outside
this Image-transient ownership claim; this is not a process RSS ceiling.
It rejects shapes whose exact output exceeds E before allocation.
Cancellation/deadlines are checked at block, CRC-slice, scan-entry and file-write
boundaries. Incompressible samples attain the same exact length; there is no
unbounded compression fallback or output-vector growth.

`exchange_limits` reports `file_cap=1024`, `byte_cap=268435456`,
`files_used`, `bytes_used`, `usage_complete`, `quota_scan_entry_cap=4096`,
`max_ancillary_bytes=65536`, `transient_image_byte_cap`,
`effective_global_retention_byte_cap` (soft),
`effective_single_entry_hard_cap_bytes`, `oversized_retention`,
`approval_available` (trusted host support, not a grant),
`available_global_retention_bytes`,
`retention_order:publication_fifo`, `retention_entry_cap`,
`activity_metadata_byte_cap`, `activity_entry_cap`, `physical_byte_cap` and
`retained_physical_bytes`. Global availability means soft target minus eligible
recovery charges, clamped to zero during oversized retention, not physical free
space. Physical ledger charges measure
snapshot/pin ownership, not the entire shared host pool or process RSS; they can
outlive eligibility through copies/pins. Authenticated observations add
`session_retained_physical_bytes`, an ownership observation, not an admission cap.
There is no separate 64 MiB session ceiling. Disconnect does not release
ownership; a new session shares the same FIFO policy. Session ownership is not
included in unauthenticated launch metadata. Export additionally reports
`max_encoded_file_bytes=E` and `max_codec_work_bytes=W`. These observations never
guarantee future allocation success.

## Confined files and cleanup

The validated descriptor runtime directory supplies the base, with the
existing XDG_RUNTIME_DIR and /run/user/<euid> fallback policy. A fresh launch
creates `<base>/vsr/<launch_id>/images` with owner-only permissions. Held
no-follow descriptors and inode checks refuse replaced, unlinked or symlinked
ancestry instead of following new pathnames. Export chooses a non-leading-dot
ASCII `image-<32 hex>.png` basename, uses exclusive nonoverwriting 0600 creation,
and tries at most 16 collisions. It never accepts an operation path argument.
This is an accident/authority boundary, not a malicious same-user sandbox.

Workers serialize quota scans and worst-case growth reservations separately
from live-state captures. Scans count agent-created regular files too, stop at
4096 entries or known quota pressure, and publish incomplete totals as lower
bounds. Incomplete scans or insufficient count/byte room reject
`busy/resource_limit`; the viewer never deletes completed files to manufacture
quota room. `affected_complete` describes bindings, while `usage_complete`
describes filesystem enumeration: they are independent.

Remove unneeded PNG working copies explicitly, including agent-created extras.
Completed files have no TTL and stay until explicit cleanup or normal launch
teardown; an uncatchable crash can leave files. Partial failed/cancelled exports
are removed relative to the held directory only when the name still identifies
the created regular file; a replacement is not owned by that partial export.
Before receipt, the host checks directory ancestry, file inode, ownership, mode,
link count, exact length and observed modification/change times again. Final
file retention transfers only when the operation engine accepts the receipt,
not speculatively before cancellation/deadline arbitration.
Normal teardown removes agent-created subdirectories recursively using held,
identity-checked descriptors. Linux `openat2` refuses symlinks and mount crossings
for recursive cleanup; every child remains linked beneath the owned root.
The constructor probes these kernel guarantees before creating the launch
exchange; kernels without the required `openat2` support advertise
`unsupported_platform` rather than promise weaker cleanup.
Replacement/relocation races stop that subtree's cleanup rather than redirect
access outside it. Cleanup never follows a replacement pathname or symlink.
Uncatchable crashes, concurrently hostile same-user changes, or filesystem/
permission failures cannot be promised successful cleanup.

PNG and undo retention are separate: deleting a PNG does not free a retained
Image undo snapshot, and restoring/evicting an undo batch does not delete its
PNG. Host loops must call
`ImageExchange::pump()` regularly on the owning thread, as they do for rendering,
to publish worker completion and release asynchronously cancelled backend slots.
Destroy the Image service before its engine; keep its allocator alive through
engine cleanup.

## Recoverable Import

The viewer and headless tutorial bind
`ImageExchange::attachRestoration(EditService&, RestoreService&)` to their
existing shared FrameBudget and authoritative Activity retention/availability
callbacks. Import stays absent on hosts lacking any of these prerequisites or
normal RestoreService's bounded result reservation. The optional RestoreService
constructor reservation uses the same host pool, never a separate or larger
pool. Qualified hosts also bind the authoritative `ExchangeLimits::retention`
ActivityLog pointer, which must outlive ImageExchange. Its effective policy is
used for both admission and reporting, not an independent Image ledger limit.
New hosts default to 4 GiB soft recovery and an 8 GiB single-entry hard cap,
without preallocation. Configure launch-local uint64 values with
`--agentRestorationBytes` and `--agentRestorationHardBytes` (hard >= soft).
Normal entries fit
by evicting complete batches FIFO only at successful publication; status reads,
replay, no-ops and failed/cancelled preparation never renew or evict history.
The disclosed 32-entry bound uses that same FIFO. Metadata/results remain
separately bounded by 16 MiB/1024 records, with eligible recovery protected
from ordinary metadata churn. Explicit host physical leases allow retained and
prepared ownership overlap (twice the larger of soft and hard plus the unchanged
256 MiB transient allowance, saturated to addressable size); the rendering
library default remains 256 MiB. An entry above soft and at most hard is retained
alone only at successful publication. The next ordinary entry returns history
to soft-budget FIFO. Exactly soft and exactly hard are inclusive. Above hard
refuses nonmutatingly with the required charge, effective soft/hard limits,
`approval_required:true` and `approval_available` indicating whether a bounded
canonical proposal was offered. Explicitly approve in the trusted local Agent
Activity window/model, then retry identical samples/type/layout/target/evidence
with a fresh `request_id` and the original refused `approval_request_id`. This is
one-success-only, expires five minutes after refusal, and never raises permanent
limits. No agent-supplied flag grants approval. See
[shared retention and approval](mesh-exchange.md#trusted-one-operation-recovery-approval)
for exact refusal/receipt fields, independent allocation safeguards and lifetime
semantics. No file, payload, decoder or transport limits have been raised.

Call `vsr_image_import` with `request_id`, `scene_id`, the Array `handle`, its
inspected `data_version`, and a confined PNG `file_name`. No prior export is
required. Finish and close externally processed files before submission.
Poll normal `image_import` status for `handle`, fresh `data_version`,
`scene_revision`, shared-namespace `batch_ref`, bounded `affected_samplers`,
`affected_users`, `affected_complete`, and `restoration_policy`.
Call `vsr_restore` with `request_id`, the same `scene_id` and
`target_batch_ref`; do not supply a data version. Agent Activity shows the
eligible Image batch with its normal local Restore action, which remains
available after disconnect or Disable agent edits. Recovery is one-way and
capacity-based, not permanent or time-based.

The service submits normal `image_import` operations with Scene READ+EDIT,
strict handle/data_version/file_name arguments, backend cleanup and historical
replay. It refills the addressed Array through **one** owning-thread write
reservation boundary and publishes an Image payload into the shared batch store.
Existing `vsr_restore` (including host-local recovery) restores prior bytes and
advances rather than rewinds data_version. It returns `image_versions` separately
from existing object/node versions. Later ordinary writes, even change-back,
permanently conflict; open maps remain untouched. Wrong-Scene retained Image
records reject before consumed/conflicting checks, without consuming eligibility.
Absent history cannot fabricate foreign-Scene evidence. Restore arguments are
strict: only `request_id`, `scene_id` and `target_batch_ref` are accepted; the
batch reference contains only `launch_id` and `batch_id`. A caller data version
is never restore authority and is rejected before request-ID admission.

Import and Image restore recheck current authority, cancellation and deadline
**after** fallible receipt/synchronization preparation. Import uses an internal
prepared host Image write: reserve once, notify maps, recheck authority/freshness
and approval, then copy samples without exposing a writable pointer. Prewrite
abort closes reached downstream maps without publishing a completed data token
or Scene revision; the reservation remains spent. Delegate leases and relocation-
safe Array lifetime evidence protect cleanup across erasure/retirement. Lease
lists are bounded to 4096 consumers and prepared before reserving the write;
exceeding that independent preparation bound safely refuses the operation. Real
callback edits retain their own freshness and ordinary `map()`/`unmap()` always
keeps its original write-tracking semantics. Restore retains its ordinary map
boundary. A failed map notification leaves the value burned, bytes/token
unchanged and the Array unmapped; a failed postwrite unmap notification still
completes the write and reports `applied_with_sync_failure`. Import store nodes/order keys and Image restore
receipt capacity are prepared before mutation; retained-batch publication and
restore eligibility consumption do not allocate replacement keys or receipts.
Retired Scene identity bindings are observed through weak lifetimes before
any pointer is dereferenced. Replacement identity domains cannot inherit a
prior batch, and retained foreign scope still precedes permanent conflict.

Input is opened once relative to the held exchange directory with no-follow,
nonblocking and close-on-exec flags. Regular-file/effective-user/single-link and
observed stat/size stability checks precede decoding the private snapshot. The
same-user process is not a sandbox: finish and close external output before
submission; adversarial concurrent same-user writes cannot all be detected.
No decode path reopens by name or decompresses embedded colour profiles.

The bounded zlib decoder validates CRCs, chunk order, target IHDR and the full
8-bit/noninterlaced channel policy before inflate. It accepts PNG types 0/4/2/6,
ignores colour/profile ancillary payloads within 65536 bytes without conversion,
and rejects tRNS/APNG, unsupported critical chunks and malformed streams. Output
is exactly T raw straight-alpha/top-down bytes. Codec allocations are F+1 filtered
bytes and a 128 KiB aligned arena supplied through zlib's allocation hooks, plus
constant controls. IDAT chunks stream from E without concatenation, including
one-byte and empty chunks. Colour/profile contents remain ignored, but these
chunks must still precede PLTE/IDAT; bad zlib trailers, dictionary-dependent
streams, concatenated streams and noncontiguous IDAT are rejected. No growing
codec/profile allocation or unchecked general-purpose image decode is used.
Along with the private 512 KiB worker stack/64 KiB guard, this is below W=4F+1 MiB;
the partitioned owner reservations below cover the remaining controls.

Preparation records originating-session physical ownership, non-destructively
reserves the shared recovery/publication resources, then reserves transient
E+D+W+S. The recovery charge includes T, the ImageSnapshot and a conservative
106,496-byte allowance for request/store/receipt ownership. Pending reservations
are distinct from eligible history; the final successful publication serializes
FIFO against current history, retaining an above-soft entry alone when at most
hard. Above-hard charges refuse without old-history loss and report the approval
requirement. The trusted bounded one-operation approval described above can
supplement ordinary authority, but cannot bypass independent physical/allocation
guards. Session/global lifetime ledgers are not separate admission caps.
S is physically allocated
once and its lease transfers to retention. Scratch releases only after buffers,
worker stack, task and future die; control/result/history charges survive their
producer. Transient use of S ends at commit without releasing its physical lease. For a 256x256 RGBA target the conservative bound is
3,212,800 bytes and the retained snapshot is 262,144 bytes (host lease overhead is
separate). Session charges survive disconnect, remain attributed to the original
session, and release on recovery/conflict/global eviction, not PNG deletion.
Reconnect gets distinct ownership attribution, not a distinct ceiling or pool.
Default activity/store limits remain unchanged. Available capacities are
observations, never future admission guarantees.

Import partitions the conservative W allowance into independently owned leases:
32 KiB Job/Engine controls, 4 KiB snapshot/store controls, 4 KiB scope/history
controls and 64 KiB Activity result capacity. Their named total is
`IMPORT_OWNER_BYTES = 32768 + 2*4096 + 65536 = 106496`, borrowed from W,
not added to the transient peak. With `peak = E+D+W+S`, the scratch lease is
`peak - S - IMPORT_OWNER_BYTES`; the one physical S lease is unchanged.
The remaining work allowance is `W - 106496 = 4*F + 942080`. Subtracting
`F+1` filtered bytes, the 128 KiB inflate arena, 512 KiB worker stack and
64 KiB guard leaves exactly `221183 + 3*F` bytes of headroom (at least
221183 bytes). This proves the partition fits those explicit allocations;
it is not a bound on platform TLS allocation or process RSS. The remaining
E/D/W lease still covers buffers, codec and worker lifetime. These
controls are distinct from logical snapshot/Activity accounting; entry overhead
remains 256 bytes, with actual result growth reserved before mutation. Result
storage is bounded to 16 KiB and covered before allocation. Engine Status,
Activity Entry and native RestoreResult value copies acquire independent 64 KiB
physical coverage before copying; the wire envelope has its own reservation too.
A copy without capacity fails rather than retaining an uncharged payload.
Prepared Activity result updates borrow the Engine's payload during its locked
sink call instead of allocating an intermediate result copy at publication.

Image consumed/conflicting and expiration controls are independently charged
and bounded to 1024 histories. Expired histories retain no foreign-Scene scope.
Restore result slots are bounded to 1024 per external/local history and never
prune active owners. Batch IDs use one launch-local, nonwrapping edit/import
counter instead of an unbounded issued-ID journal. Copy-held reservations can
outlive Engine/Activity eviction or host teardown; they release when those
copies die, not when the original operation completes.

The spec's illustrative E=2 MiB, D=4 MiB, W=17 MiB, S=4 MiB needs
27 MiB transient and 4 MiB retained, not 31 MiB or two snapshots. Production
admission reserves the conservative target-dependent E and W maxima, not that
example's smaller E: for 1024x1024 RGBA it reserves 34,674,688 bytes plus host
lease overhead and retains exactly 4,194,304 snapshot bytes. A 27 MiB
configured transient ceiling correctly rejects that conservative reservation.
Loopback qualification measures the public physical pool and independent
session ledger across transfer, pressure, rejection, cancellation, recovery,
result copies, eviction and teardown. PNG cleanup and undo retention remain
independent.

## Isolated eligible-Image verification

After build/install, run `python3 scripts/test-image-export.py build-agent`.
It creates a private authenticated Xvfb, runtime/configuration and asymmetric
3×2 glTF Image, launches the actual viewer with the installed libraries and
Helide, and exercises authenticated discovery, Array inspection, export,
independent CRC/zlib/sample checks, deleted-file historical replay and normal
shutdown cleanup. It does not attach to an existing viewer or alter Pi config.
Add `--import` to verify external raw-channel processing, two shared Samplers,
Import, deleted-input replay and exact normal Restore with fresh image_versions.
The UI-model suite verifies Agent Activity's disconnect/Disable edits/local
Restore route. For an actual UI pass, add `--import --local-ui`: the script
pauses at the disconnected eligible Image batch, saves a private screenshot
and display metadata under the build directory, and waits up to 180 seconds.
Use only that private display: check Disable agent edits, click its Restore,
then create `<build>/t05-ui-proceed`. The script reconnects, checks Import is
forbidden while READ Export still works, and compares all recovered bytes.
Whole-feature chess/presented-camera acceptance is separate.
