# Rolling agent recovery: operator guide

Image, Mesh and existing parameter/Node batches use one shared recovery pool.
This is **exact, conditional, one-way recovery of an individual committed batch**,
not a linear undo/redo stack. A retained batch may be restored out of order only
while its dependencies still match. Restore consumes that batch; there is no redo.
Later writes, including a change back to identical data, can permanently conflict.

## Defaults and launch configuration

New integration-enabled viewer and headless hosts use binary byte limits:

| Setting | Default | Launch option |
|---|---:|---|
| Soft retained-history target | 4,294,967,296 bytes (4 GiB) | `--agentRestorationBytes` |
| Single-entry hard cap | 8,589,934,592 bytes (8 GiB) | `--agentRestorationHardBytes` |

Options accept checked positive uint64 decimal bytes; hard must be at least soft.
They are launch-local, not changes to a running host. Explicit configuration is
preserved. Neither default preallocates memory or guarantees allocation success.
These limits concern fully accounted host recovery ownership, **not** encoded
working-file size, total process memory, Scene storage, transient work or VRAM.
Independent payload, format, decoding, transport and allocation limits still apply.

## Which batches survive?

Let `charge` include all recovery data, retained shared Arrays and control/result
ownership, not merely copied texels or the input file's size.

| New batch charge | At successful publication |
|---|---|
| At most soft | Evict oldest whole batches until the newest suffix fits soft. |
| Above soft, at most hard | Evict all older batches; retain this batch alone. |
| Above hard, without valid approval | Refuse before Scene mutation or history eviction. |
| Above hard, with valid bounded approval | If all other checks pass, evict older batches and retain this batch alone. |

Exactly soft is normal; exactly hard needs no above-cap approval. The next change
uses the same table: a new normal batch evicts an older oversized batch even if
the two would fit below hard. Exceptional retention is not a permanent mode.
FIFO is successful **publication order**, not arrival, preparation, timestamp or
access order. Reading status, attempting Restore and replaying requests do not
refresh age. The separate **32-whole-entry** recovery bound also evicts FIFO.

Validation, authority/freshness checks, ownership, allocations and publication
resources are prepared before eviction, then rechecked against current history
at the commit boundary. Invalid, failed or cancelled precommit work preserves
Scene and prior recovery. A true no-op creates no entry and evicts nothing.
Historical replay neither recommits nor repeats eviction. After publication,
`applied_with_sync_failure` is still `committed:true`, with the new recovery
retained; it is not a rollback or a safe reason to apply the edit again.

## Approve one operation, not a larger permanent budget

1. Submit the ordinary Image/Mesh Import or parameter/Node edit. A validated,
   changed above-hard proposal returns `busy/restoration_approval_required` with
   `required_capacity_bytes`, `configured_capacity_bytes` (soft),
   `effective_soft_limit_bytes`, `effective_hard_limit_bytes`,
   `approval_required:true`, `committed:false` and `history_unchanged:true`.
   `approval_available` says whether a bounded proposal was offered, not granted.
2. In the trusted local **Agent Activity** window, inspect the session/request,
   Scene, target/evidence, fully charged required bytes, finite maximum and expiry.
   Approval warns that success evicts all older recovery, retains this entry
   alone, leaves permanent limits unchanged and may still fail allocation.
   Choose **Approve this one operation**, or **Decline**.
3. Retry identical contents with a fresh increasing `request_id` and the original
   refused `approval_request_id` in the same session. Ordinary READ/EDIT, enabled
   agent edits, current original dependency evidence and all other guards remain
   mandatory. That request field references a trusted decision; it cannot grant it.
4. Poll status; use the successful receipt's normal `batch_ref` with `vsr_restore`.
   One successful commit consumes approval even if synchronization then fails.
   Future operations still use the unchanged soft/hard limits.

There is no MCP approval-grant endpoint. A bundle flag, ordinary edit permission,
startup configuration or approval of a feature specification is not one-operation
approval. The trusted local API is
`IntegrationActivityModel::refresh().recoveryProposal`,
`approveRecovery(identity, requiredBytes)` and `revokeRecovery(identity)`.
Headless operators may expose these same local controls. This implementation
approves **exactly** the fully charged required maximum, not an arbitrary ceiling.

One live canonical proposal per launch Activity log is bounded to **64 MiB total**
contents/control storage and **300 monotonic seconds from the original refusal**.
The host reserves physical coverage before copying it. Reads, approval and retries
never renew the TTL. Another proposal cannot replace a live one; decline/revoke it
or let it expire. The canonical identity binds launch/session/kind/Scene/original
request, exact charge and actual contents: Image samples/type/layout, Mesh carrier
bytes/actions/aliases and converted parameter/Node final state. Matching filename,
size or mtime is not enough; changed content requires a new proposal.

Exclusive claims prevent concurrent reuse. Precommit failure/cancellation releases
the claim without consuming approval, but retries need fresh IDs and current
original evidence. Session end, revocation, lost registration/identity or expiry
invalidate approval and pending claims. Historical replay returns the original
result without renewing or consuming approval. None of this bypasses format,
confinement, freshness, physical capacity or actual allocation checks.

## Read capacity and recovery status honestly

Authenticated discovery's `exchange_limits` reports effective soft/hard limits,
`retention_order:"publication_fifo"`, `retention_entry_cap`,
`oversized_retention` and `available_global_retention_bytes`. Available retention
is soft minus eligible charges, clamped to zero during oversized retention; it
is not free physical memory or a reservation. Commit `restoration_policy` records
limits, required charge, oversized status and possible eviction historically.
A historical receipt does not prove that its `batch_ref` remains recoverable.
Evicted references permanently report unavailable recovery (`reason:expired`).

Activity metadata/results are separately bounded to **16 MiB / 1024 records**;
eligible recovery and active work are protected from ordinary metadata churn.
There is no separate legacy 64 MiB session recovery ceiling. Sessions share FIFO;
disconnect neither creates another pool nor releases outstanding ownership.
`session_retained_physical_bytes` is authenticated ownership attribution, not an
admission cap. Launch descriptors contain no session ownership observation.

`physical_byte_cap` and `retained_physical_bytes` describe independent physical
ownership limits/accounting, not process RSS. Eviction removes eligibility and
releases retention-owned resources, but copies, pins, active Restore and deferred
owners stay lifetime-safe and charged until actual release. Active preparation
is not evictable history. The host physical pool allows preparation overlap:
2 × max(soft, hard) + 256 MiB, saturated to addressable size (17,448,304,640 bytes
at defaults). It is not preallocated; other owners can still exhaust it. The
rendering library's independent FrameBudget default remains 256 MiB.

## Files, persistence and rendered correctness are separate

Eviction/Restore never deletes PNG or Mesh working copies, rewrites original
assets or saves the Scene. Deleting a working copy does not free recovery.
Completed files have no TTL; explicit cleanup and normal launch teardown are
separate from undo retention. Ordinary Scene Archives persist current Scene
state independently of the in-memory batch history. Retention changes do not
restart or redeploy a running viewer or change its unsaved Scene/settings.

**Known limitation — Barney Image rendering:** with the currently unfixed native
Barney texture-refresh path, Image Import can commit correct CPU bytes while the
bound device still renders a stale texture. Actual incremental Image update/restore
rendering qualification on Barney is deferred until that backend defect is fixed;
correct Barney Image refresh is not claimed. Shared retention and conditional CPU
recovery checks still apply. This does **not** defer Barney Mesh qualification or
Image rendering qualification on Helide and a patched VisRTX library. Plugin
identity matters: an older unpatched VisRTX plugin can also render stale textures.
Do not infer rendered correctness from receipt revisions, CPU recovery or Mesh
colours, and do not recreate devices/Arrays to conceal stale Image results.

See [Image PNG workflows](image-png-exchange.md),
[Mesh workflows and approval details](mesh-exchange.md),
[Mesh persistence](mesh-exchange-persistence.md), and the separate
[Image](image-device-qualification.md) and
[Mesh](mesh-device-qualification.md) same-device rendering procedures.
