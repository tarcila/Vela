# 03 continuation — CPU observation ownership proof

Qualified target: this workspace's Linux/glibc/libstdc++ RelWithDebInfo build,
CUDA/UI/network/Lua/SDL3 ON, synchronous HOST ImagePipeline/Helide producers.
This is an observation-owned allocation ceiling, not a process RSS/address-space,
Scene/render-device, generic transport infrastructure or GPU-memory ceiling.
The actual viewer/barney/CUDA presentation adapter remains unqualified/off (04).
All source allocations below draw from one shared pool, at most268435456 bytes.

## Allocation bounds and ownership lifetimes

| Owner | Reservation before allocation | Lifetime / included overhead |
|---|---|---|
| FrameBudget | `sizeof(FrameBudget)+256` baseline | Object, shared control, allocator headers; smaller pools invalid. |
| Every lease | requested bytes + `sizeof(Lease)+256` | Lease/control/allocator headers, including empty-result/delivery leases. Checked subtraction precedes addition; allocation failure cannot change the counter. |
| Service | `sizeof(FrameService)+1MiB` | Maps' fixed state, mutex/CV, alive control, at most64 wake copies, callback additions to the existing bounded engine, thread/TLS/tcache/control allowances. No new transport pool. |
| Encoder stack | `2MiB+64KiB+128KiB` | Explicit anonymous user stack, PROT_NONE guard and additional runtime bookkeeping. Setstack uses this mapping; join precedes munmap and lease release. No libc default-stack cache. Failed setup cannot attach/expose a source. |
| View | `sizeof(View)+256KiB` each, at most64 | Map node, CPU producer object, 32-byte authorized observer string, bounded persistent tracker/configuration strings. No transport accesses these host-only values. |
| Job | `sizeof(Job)+16KiB` each, at most64 | Lookup node/key, Job/control, Work, packaged task/future state, bounded executor/wake captures and active engine request bookkeeping. Admission validates the session/ref before key allocation. The lease follows the Job held by engine/worker, not merely the table. |
| Production/observation | `8*64KiB+sizeof(Production)+4096` / `8*64KiB+4096` | Reserve before bounded binding observation/validation/serialization. Includes nested settings, strings/control blocks and temporaries; a matched receipt keeps its lease. Qualified host callbacks must themselves obey bounded metadata/capacity/no-side-effect contracts. |
| CPU snapshot | `4*width*height+sizeof(CpuFrameSnapshot)+8192` | Fresh exact-sized RGBA vector, immutable snapshot/control, allocation headers/page rounding. Latest is cleared before acquiring a newly submitted frame; allocation failure cannot revive an older frame. Held consumers keep the lease. |
| Encode scratch | `16*(nativeRGBABytes+height)+8MiB` | Existing fixed-quality encoder normalization/filter/output/realloc/fallback overlap plus hash-table/list allocations and headers. The prior encoder proof gives output<=2*filtered+8 and old/new growth overlap<=12*filtered. Remaining4 filtered arrays cover normalization/filter/final overlap. 16384 hash lists with capacity<=30 pointers, 256-byte conservative per-list overhead and128KiB table fit within8MiB. No encoder/cap changes. |
| Retained PNG | proved PNG bound + `sizeof(EncodedFrame)+8192` | Exact reserved final vector/control and page rounding; separate from scratch. Retained refs/results and old images retain ownership across resize/expiry/table removal. |
| Metadata record | `2*64KiB+8192` available;8192 unavailable | Bounded payload plus retained engine record/callback/map bookkeeping. Moves transfer the lease through Step to the record. This allowance remains after Job expiry. |
| Visible public Status | `8*(3*64KiB+16384)` available,16384 unavailable | Reserve before copying the protected payload, including replay and generic lookup/cancel. Status carries the lease; statusResult transfers it to ToolResult, then HTTP. Repeated held status copies exhaust truthfully instead of remaining uncharged. |
| Frame delivery | `8*completeResponseBound+4096` | PNG base64, concatenation/string capacity/reallocation overlap, text/structured duplicate metadata, JSON-RPC and HTTP/socket drain. Metadata is reserved before copying. Full carrier cap12MiB remains; serialized RPC ID<=1024 and envelope arithmetic are checked before protected serialization. Slow sends keep the lease through completion/timeout. |

Frame dimensions are checked against2048-axis/2073600-pixel and size_t arithmetic
before snapshot allocation. These checked bounds make subsequent scratch products,
PNG bound, base64 rounding and12MiB-response/eightfold reservations representable
on the qualified target. FrameBudget admits only when both payload and lease
headers fit after subtraction. It updates used bytes only after allocation.
There are no hidden rescale, replacement render or mutable-buffer consumer paths.

Rendering code propagates physical allocation exceptions without counter changes;
the application-facing FrameService presentation/begin-production/encoding
boundaries preserve natural display and return truthful unavailable results.
MCP allocation exceptions unwind response leases and close the connection rather
than terminating the host. Actual physical OOM injection is **not** claimed;
checked refusal, exception-safe ownership and memory tools are the evidence here.
The existing encoder itself is unchanged, not newly OOM/sanitizer qualified.

Current authority is checked at admission, work, publication and retrieval.
Withdrawal retires the source and removes unheld jobs/PNGs at pump. In-flight
encoding retains immutable snapshots and its slot until draining; cancellation
cannot publish. Already-published HTTP bytes cannot be retroactively retracted,
so their immutable lease remains until drain/close. A future source/device
requalification is cold, even with the same Device Identifier.

Unused shared allocator arenas/free caches and pre-existing generic MCP listener,
connection threads/input parsing are runtime infrastructure, not live owned frame
allocations. Their exclusion is not a claim of zero per-allocation or worker
bookkeeping: headers/page rounding and observation-created stack/control/cache
allowances above are charged. Client-made PNG decodes, raw protocol parses and
copies outside this ownership interface are client memory. Natural Scene,
RenderIndex, ImagePipeline render buffers and SDL display targets are not fetched
or allocated by observation and are outside this ceiling.

## Independent runtime evidence (not counter-only proof)

- `strace -f -e trace=mmap,mprotect,munmap ... 'CPU observation charges*'`
  exited0,1 case/17 assertions. The log records the same2162688-byte anonymous
  mapping, successful65536-byte PROT_NONE guard, and successful2162688-byte
  munmap after join. Stack mapping therefore matches the charged bound rather
  than an assumed std::thread default. Log: `logs/03-cont-stack-syscalls.log`.
- Valgrind Memcheck `[presented-frame]`: exit0,20 cases/3800 assertions,
  **0 errors,0 definite/indirect lost bytes**. 5304 bytes possibly lost in22
  blocks,4268598 still reachable,32 suppressed bytes; ioctl/address-range
  warnings disclosed. Includes both actual HTTP sections and source pressure.
  Log: `logs/03-cont-qualified-valgrind.log`.
- Massif `[frame-memory]`: exit0,6 cases/2718 assertions. It profiles all process
  heap, not just the counter. Whole sampled heap+allocator overhead peaks at
  **341415416 bytes**, which correctly exceeds256MiB because real render/display
  and client allocations are outside the observation ceiling. This is not hidden.
- `03-memory-audit.py` removes only explicitly named existing ImagePipeline
  buffer and SDL allocation subtrees; unknown, Scene, client and all allocator
  overhead remain in the residual, deliberately overcounting observation heap.
  Across53 detailed samples, remaining process heap peaks at126861040 bytes;
  adding the separately observed encoder stack/guard gives**129023728 bytes**.
  See `03-memory-audit.json`. This is a sampled conservative residual, not an
  exact attributed/all-time memory peak; the allocation bounds above and lifetime
  counter gate establish the ceiling. No absent-match inference is used.
- Two authorized real CPU sources/Scenes and distinct devices approach the
  shared ceiling with maximum1920x1080 native frames, retained old PNGs and real
  delivery results: Memcheck sampled peak**265406669 bytes** (below268435456),
  followed by truthful resource exhaustion. Both normal producers keep running;
  independent decode verifies explicitly selected source pixels.
- Actual loopback socket receive-window backpressure holds two large image
  responses; a third is busy. High-entropy1920x1080 PNG is**8296191 bytes**,
  within8MiB; normal-run held reservations are about205MiB decimal, not an
  unsupported total-process-memory claim. Successful socket drain releases
  leases. Another section cancels deferred encoding and withdraws read while a
  large response is stalled, proves minimal new receipts/no image publication,
  normal viewing continuation and eventual work/socket release.

No GPU/current-barney, sanitizer, monitor/compositor parity, physical-OOM stress
or whole-spec completion is claimed. Full final regression counts and lineage
are in `03-verification.md`; outer-orchestrator independent review remains pending.
