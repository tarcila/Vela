# Inspect Image data and track ordinary writes

Array inspection retains its existing summary and adds mandatory `mapped`
(Boolean) and `image_info` (object). Sampler image references remain opaque Array
links with summaries, not duplicate Image payloads. No texels are downloaded.

`image_info.state` is:

- `available`: host-resident Array2D, positive width/height (including 1×1),
  signed/unsigned 8-bit fixed-point linear or unsigned sRGB, one to four
  channels, automatically enrolled on a serialized host. Includes
  `data_version` (opaque equality-only string), `dimensions: [width,height]`,
  `element_type` (ANARI name), `channel_count`, `color_space: linear|srgb`, and
  `row_order: top_down`.
- `unsupported`, `reason: unsupported_image`: other dimensions/types, zero
  dimensions, CUDA/proxy/object storage. No conversion or materialisation.
- `unavailable`, `reason: host_write_contract_unavailable`: eligible storage
  without the registered host prerequisite or maintained enrollment. No
  fabricated version or format payload.

Availability describes supported storage/tracking, not edit permission or an
unmapped state. `mapped` observes a real outstanding map on the owning thread.
Inspection/read-only `data()` access never advances a completed-write token.
At the owning-thread observer boundary, map admission advances the inspection
revision and invalidates pagination cursors, without advancing the Scene revision,
Array parameter version or completed-write token. Completed unmap advances the
inspection and Scene revisions and Array parameter version once, invalidates
cursors again, and publishes the reserved token. Nested-map rejection changes none
of these observations. During a map, inspection keeps the previous completed token.
Each successful map/unmap cycle advances exactly once, even a same-value write or
change-back.
An unmap publishes before downstream notification: notification failure does
not roll back bytes or pretend the write did not complete.

Qualified viewer/headless hosts now expose [PNG Image Export](image-png-exchange.md).
Discovery distinguishes export availability from the `data_tracking` prerequisite
and still reports `import:false`. Tracking alone is not file-exchange authority;
unsafe/unavailable directories remain unavailable. Image Import and Image undo
are not implemented.

## Host prerequisite

`SceneRegistration::serializedArrayWrites` (also on `ContextSceneOptions`)
defaults false. Opt in only when all supported Array map/write/delete operations
run on the registration/service owning thread, serialized with integration
transactions, without service reentrancy. The viewer and headless tutorial opt
in for their single-owner loops. A custom network/worker host must marshal all
writers to this thread before opting in; raw-pointer and arbitrary worker writes
are unsupported. The ANARI VSR device's shared application memory is not live
Scene Array memory: its synchronization copy still uses `setData`; externally
mutated application buffers do not establish this host prerequisite.

Registration enrolls all existing eligible Arrays before exposure; Scene Array
creation enrolls future ones before their first write. Proxy conversion enrolls
when the Array becomes host-eligible, not while it is a proxy. Registration
refuses an existing outstanding eligible map or a different launch clock. Scene
and Array layers depend on no integration code.

All tracked Images in an Integration share one `ArrayWriteSequence`. Mapping
reserves/burns a launch-wide value **before** returning writable bytes. Failed
map notification burns its reservation without publishing it. No value is
returned to the allocator, and out-of-order completion across Images is valid.
Tokens bind launch, SceneIdentity, live Array lifetime and completed write state;
they are equality-only, not sortable sequence numbers. A smaller constructor
ceiling supports bounded exhaustion tests without changing token identities or
introducing wrap/reset. Exhaustion rejects further tracked maps and `setData`
before mutation, while inspection, const reads and unrelated parameter edits
remain usable. Future replacement/restore transactions must use this same
reservation boundary rather than a separate clock.

Disconnect does not remove enrollment. Owning-thread
`Integration::setImageExchangeAvailable(sceneId,false)` temporarily withdraws
inspection support without stopping ordinary tracked writes or losing history.
If the host cannot maintain tracking, call `imageWriteSequence()->stopWrites()`
**before** withdrawing support. Existing mappings can still complete; no new
tracked map is admitted. Recovery must never silently re-enroll with a new clock
behind retained expectations.

## Mutation and lifetime audit

- `Array::map` rejects nested mapping; `unmap` without a successful map rejects.
  Rejected nested `setData` never unmaps the caller's mapping.
- All `setData` overloads pass through map/unmap. Typed ranges/null sources are
  validated before mapping. FILE writes publish once on completion, including
  partial reads (they may have changed bytes). Proxy setters remain no-ops.
- Import/clone, Lua scoped maps, UI array writers and network transfer use these
  same entry points. They inherit reservation refusal; no caller should catch a
  refusal and then write through `data()`. Const read/copy paths do not reserve.
- Array copy is disabled. Pool relocation/moves carry lifetime, mapping,
  outstanding reservation and completed state; the moved-from object cannot
  publish it. Retirement clears a non-owning lifetime pointer. Raw replacement
  of a live pooled Array is not an in-place texel writer: it retires its previous
  identity, and SceneIdentity cannot resolve the old token against a replacement.
  Move assignment replacing a still-live tracked Image is refused before storage
  changes; explicit Scene deletion followed by creation is the retirement route.
- Creation/removal/defragmentation on a tracked Scene enforce the owner thread.
  This is not arbitrary worker-thread safety, a general Scene lock or a promise
  to protect retained raw pointers against deletion.

The Image Cache resolves a weak live Array lifetime before returning an entry
or building a Sampler. Pool-index reuse cannot return an unrelated Array.
Relocation preserves identity; deletion/Scene teardown retires it without an
owning hold preventing deletion. Same Image Source/Color Space/Row Order
acquisition returns the same live Array and its changed bytes with no implicit
reload. Sampler release and exchange budgets do not evict it. Explicit cache
clear removes entries, not live Arrays; explicit Array deletion retires that
entry and a later acquisition may decode anew. Caches must not outlive their
Scene. See [ADR 0014](adr/0014-store-images-in-anari-orientation.md).
