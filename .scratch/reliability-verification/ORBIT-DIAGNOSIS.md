# Orbit settlement diagnosis — 2026-10-03

Source of intent: read-only sibling workspace ticket
`../broad-editing/.scratch/reliability-verification/issues/01-orbit-settlement.md`.
No separate spec/addenda supplied. Copied ticket explicitly jj-tracked here.
Existing skills setup is already recorded in AGENTS.md/docs/agents and history.
Prior GPU commit 17ee63e9 is untouched. The pre-existing empty root
sentinel-dbg-wrapper.stderr was moved into this scratch directory, not committed.

## Phase 1/2: minimized real-protocol red

Command: `bash .scratch/reliability-verification/orbit-phased.sh`.
One real helide server, one real client mirror, off-centre orbit, position-only
edit, ordinary unrelated metadata receipt, then direction/up and another receipt.
A delegate installed before the server loop copies camera and rig state on that
owning loop thread; a mutex publishes values to the caller. No live Scene reads
are needed for the minimized signal. Each receipt is sent on the same ordered
edit stream, after the preceding camera messages. Its key is not manipulator.*,
so production does not follow it as a view update.

Red log phased-runs/run-OHk541/output.log: **1 case, 23 assertions, 1 failure**;
13.0 versus 16.2271003723 at original requireNear's 1e-4 tolerance. Two full
orbit-filter runs independently reproduced exactly those values (run-sO4fPY,
run-0B1SHz). This exactly matches the prior parallel red numerical symptom.
Position is already (20,0,0) when the first rig centre is 16.2271; the complete
pose yields 13.0, even without unrelated uiCollapsed. The incomplete observation
is the comparison's expected side. No arbitrary delay or retry drives the red.
The two phases are load-bearing: no early receipt means no partial baseline;
no later direction/up means no completed follower change.

## Phase 3: ranked falsifiable hypotheses (before causal correction)

1. **Position-only settlement observes an incomplete pose.** Prediction: the
   position receipt satisfies the old predicate, but direction/up still differ;
   complete-pose receipts reject the partial state and retain centre 13.0 across
   unrelated metadata. The minimized red already establishes the first part.
2. **Unsafe cross-thread camera/rig reads also permit transient/torn orbit
   observations.** Prediction: a receipt after all orbit messages, copied on the
   loop thread, gives metadata, pose and rig agreement without the occasional
   small centre mismatch. A metadata.at-only read is not proof follower code
   has returned (metadata notifications precede followCameraEdit).
3. **Production re-applies stale manipulator metadata for unrelated keys.**
   Prediction: after a valid complete-pose receipt, uiCollapsed followed by a
   receipt changes rig centre back to (3,-4,5), rather than keeping (13,0,0).
   If observed, fix production; otherwise preserve production logic.
4. **Shared process/filesystem state causes the mismatch.** Prediction: this
   minimized signal disappears with isolated HOME/XDG/TMPDIR/free port, or
   remains after a complete barrier only under suite parallelism. The red above
   already survives isolated state and needs no other process; full -j4 and
   focused repeats will probe the remaining possibility, not prove zero risk.

## Planned probes and correction

Inspect partial versus complete position/direction/up copies, first record the
old position-only predicate failing the regression that it must reject partial
state. Then change only the barrier to require the complete pose. Preserve all
original camera, orbit, rig, dirty/revision, no-echo and unrelated-key intent and
numerical comparisons, replacing live cross-thread observations with receipts.
Keep the phased scenario as a strict regression: partial must not settle, full
must settle, unrelated metadata must not change the fully followed rig.

## Phase 4/5: probes and correction

- Predicate-red log phased-runs/run-U7Jf22/output.log: **1 case, 23 assertions,
  1 failure** at `REQUIRE_FALSE(poseSettled(partial,...))`: the old position-only
  predicate returns true. The regression existed and failed before correction.
- With a complete predicate and owning-thread receipts: partial position is
  delivered but direction and up are not yet the requested values; a second
  phase delivering direction still does not satisfy the full pose; delivering
  up finally settles it. Rig centre is (13,0,0), unchanged by uiCollapsed.
- A deliberately wrong production negative control changed the existing
  manipulatorChanged guard to also follow uiCollapsed. After build **and
  install**, the same phased regression failed **3.0 versus 13.0** (1 case,
  41 assertions, 1 failure; phased-runs/run-Hcnswh/output.log). This distinguishes
  a real stale-metadata overwrite from the 16.2271 premature baseline. The
  production probe was reverted, rebuilt and installed; no production diff
  remains. It is not the shipped correction.
- Shipped correction: only test observation/barriers change. The receipt key
  travels through existing SetObjectMetadata, on the same ordered client edit
  stream. Its observer reads on the server loop thread after prior applyEdit /
  followCameraEdit calls complete, publishing deep value copies under a mutex.
  Server/project pointers stay inside that observer, registered before run().
  OrbitReceipts outlives the server and observer. The test thread reads only
  copied values and its own client mirror/replica. No new production API.
- All original orbit, rig, reconstructed-manipulator, revision/dirty/no-snapshot
  and unrelated-metadata comparisons remain, including requireNear's original
  1e-4 tolerance and Approx distance checks. Settlement additionally verifies
  all nine pose coordinates delivered exactly through the protocol. Existing
  arbitrary 100/200 ms observation waits are removed from this scenario, not
  increased. Neither sleeps nor retry is a fix; repetitions below are checks.

## Phase 6: verification

Every bash invocation ran from `/home/tarcila/Code/ANARI/vela/broad-editing-cuda`.
The scripts set HOME/XDG/TMPDIR beneath this scratch directory, use helide for
Studio and prepend this CUDA workspace's installed/build libraries. They do
not configure CMake, modify global/user state, or move bookmarks. The existing
Ninja Release CUDA build/cache and GPU tests remain intact.

- `cmake --build _out/_cuda-check -j4` and
  `cmake --install _out/_cuda-check`: passed after reverting the negative
  control (orbit-final-build.log / orbit-final-install.log).
- `clang-format --dry-run --Werror src/tests/test_StudioRemoteE2E.cpp`: exit 0.
- `bash .scratch/reliability-verification/orbit-phased.sh`: green **39 assertions
  / 1 case** (phased-runs/run-71X7ea/output.log).
- `seq 1 20 | xargs -P4 -I{} bash .scratch/reliability-verification/orbit-phased.sh`:
  **20/20**, each **39 assertions / 1 case**, counts checked separately via
  Python; orbit-phased-final-repeat.log.
- `seq 1 10 | xargs -P4 -I{} bash .scratch/reliability-verification/orbit-repro.sh`:
  **10/10**, each **103 assertions / 1 case** across all four THEN paths,
  counts verified from actual output files; orbit-final-repeat.log. Original
  helper's exit status alone was not accepted as evidence.
- `bash .scratch/reliability-verification/verify-orbit.sh full` executes original
  `ctest --test-dir _out/_cuda-check -j4 --output-on-failure`: **68/68**, 21.99 s;
  final-runs/full-sl8PtM/output.log.
- `bash .scratch/reliability-verification/verify-orbit.sh full-repeat` executes
  that full -j4 suite with `--repeat until-fail:3`: **204/204 test executions**,
  68/68 suites all three times, 55.76 s; final-runs/full-repeat-0zK4EK/output.log.
- `bash .scratch/reliability-verification/verify-orbit.sh heavy-repeat` executes
  `ctest --test-dir _out/_cuda-check -j4 --output-on-failure -R
  '^vsr::(StudioRemote|StudioServer|StudioClient|StudioTestClient)$'
  --repeat until-fail:10`: **40/40**, 185.51 s;
  final-runs/heavy-repeat-fNVKAt/output.log.
- `bash .scratch/reliability-verification/verify-orbit.sh gpu` executes
  `_out/_cuda-check/vsrTests '[CUDA],[Downsample]'` and
  `compute-sanitizer --tool memcheck --leak-check full --error-exitcode 99
  _out/_cuda-check/vsrTests '[CUDA],[Downsample]'`: both **98,792 assertions /
  3 cases**; **0 errors, 0 bytes leaked**; final-runs/gpu-050RQS/*.log.
  These are real cudaMalloc/kernel tests, not a CPU substitute. No GPU-test
  code was amended. Prior profiler/initcheck/racecheck/synccheck evidence in
  REPORT.md remains unchanged; no binary profiler artifact was read or added.

## Conclusion and remaining limits

The exact large mismatch is deterministically explained by a premature
position-only baseline: 16.2271 is the real partial-pose follower centre,
13.0 is the completed pose centre, not stale metadata reapplication. The
unrelated-key negative control would instead revert to 3.0 and is caught.
The original metadata.at wait also read concurrently and could return before
its follower and subsequent pose edits finish; owning-thread receipts remove
that invalid observation altogether. This run does **not** claim to have
replayed the exact tiny rounded 3.0-versus-3.0 failure or proven a particular
torn-read interleaving caused it. The original unsynchronized reads were
invalid independent of its precise interleaving. Complete snapshots satisfy
all original numerical intent without any production correction.

Finite green samples do not prove a zero failure rate or CUDA independence.
Other remote scenarios still use live server-state reads; auditing them is
outside this ticket's orbit-settlement scope. No new spec decision or hole
was needed. No sleep/retry/CTest serialization/tolerance relaxation was used
as a fix, and no adjacent scenario was changed.
