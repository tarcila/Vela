# Export a live Image as a PNG working copy

The integration-enabled viewer and headless tutorial register `vsr_image_export`.
Image Import is not implemented. A successful export is a read, not an edit,
render, scene save, or original-asset write. It needs current Scene READ, even
when agent edits are disabled; it does not map the Array, reserve a write
sequence, change texels, or occupy a presentation/render encoder slot.

Discover the Scene and its `image_exchange` observation. On qualified Linux
hosts it reports `supported`, `enabled`, `export`, `import:false`,
`data_tracking`, `image_exchange_directory`, and `exchange_limits`.
Unsupported platforms or unsafe runtime directories fail closed. Directory
metadata is currently delivered through authenticated discovery; the strict
connection descriptor remains unchanged. Do not infer directory availability
from the tracking prerequisite alone.

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
copying. It has no undo snapshot. The ceiling is the minimum of 268435456 and
the configured host `ExchangeLimits::transientBytes`; pressure rejects
`busy/resource_limit`. A host may also supply a lifetime-stable, thread-safe
`std::pmr::memory_resource` for capture/output physical-memory accounting.
Allocator and worker-start failures unwind reservations and reject busy.

The export encoder is separate from the RGBA presentation encoder. It emits
one filter-0 byte per row directly from the capture into stored DEFLATE blocks
of at most 65535 bytes. With `B = ceil(F/65535)`, the exact PNG length is
`F + 5*B + 63`: 6 zlib framing bytes, 5 per block, and 57 PNG framing bytes.
It reserves that exact output once, emits no separate filtered buffer, and
uses constant codec state; actual codec work is below the conservatively
reserved W. It rejects shapes whose exact output exceeds E before allocation.
Cancellation/deadlines are checked at block, CRC-slice, scan-entry and file-write
boundaries. Incompressible samples attain the same exact length; there is no
unbounded compression fallback or output-vector growth.

`exchange_limits` reports `file_cap=1024`, `byte_cap=268435456`,
`files_used`, `bytes_used`, `usage_complete`, `quota_scan_entry_cap=4096`,
`max_ancillary_bytes=65536`, `transient_image_byte_cap`,
`session_snapshot_byte_cap=67108864`, `effective_global_retention_byte_cap`
and `available_global_retention_bytes`. Export observations additionally
report `max_encoded_file_bytes=E`, `max_codec_work_bytes=W` and
`available_session_snapshot_bytes=67108864`. Before Import exists there are no
Image snapshot ledger charges. Session-specific availability is not included
in unauthenticated launch metadata. Available capacities are observations,
not future-admission guarantees.

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
are removed relative to the held directory. Cleanup never follows replacement
paths or agent subdirectories. Agent-created subdirectories can prevent final
directory removal and must be cleaned explicitly.

PNG and undo retention are separate: deleting a PNG does not free a retained
Image undo snapshot, and restoring/evicting an undo batch does not delete its
PNG. Import and Image undo are not yet exposed. Host loops must call
`ImageExchange::pump()` regularly on the owning thread, as they do for rendering,
to publish worker completion and release asynchronously cancelled backend slots.
Destroy the Image service before its engine; keep its allocator alive through
engine cleanup.
