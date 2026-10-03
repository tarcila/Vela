# 01 — Diagnose remote orbit settlement before capture work

**Status:** resolved
**Blocked by:** none
**What to build:** Establish whether the observed parallel remote-camera comparisons indicate an early/unsafe test snapshot or a genuine server state overwrite, then make only the evidenced reliability correction with a strict regression.

## Observed signal

CUDA-on full CTest -j4 failed the existing client-orbit scenario twice: one small centre mismatch and one 13.0 versus 16.2271 mismatch after an unrelated metadata key. Serial68/68, isolated StudioRemote10times, focused orbit10parallel4, and four heavy Studio suites10repeat-j4 subsequently passed. These green samples do not erase the earlier red observations or prove determinism.

The focused command is the CUDA workspace's scratch orbit-repro.sh. Two red logs are ctest-full.log and ctest-parallel-repeat.log. The source scenario waits for metadata at only, and later for camera position only before retaining the rig state; direction/up are separate updates. The fixture explicitly documents that callers may touch the server scene/project only before its loop starts, but the scenario polls those live fields from the test thread.

## Acceptance criteria

- [x] Follow diagnosing-bugs: construct/minimize a fast red-capable loop through the real client/server orbit path before a causal fix. Controlled phased packet delivery is preferable to arbitrary sleeps or probabilistic retry.
- [x] Distinguish incomplete pose delivery, real unrelated-metadata orbit reapplication, unsynchronized test reads, and shared test-state interference with falsifiable predictions; record ranked hypotheses once the loop is established.
- [x] Preserve all original numerical tolerances and camera/rig/metadata intent. No sleep, serialization of the entire CTest run, blanket skip, or relaxed comparison may masquerade as a fix.
- [x] Use public protocol and existing fixture/observer/safe-point facilities. Copies published by the owning server thread may make the harness reliable; no new production test-only API or live cross-thread Scene access is introduced.
- [x] Add the narrow regression first and see it fail. Only change production following/state logic if the same failure remains after valid complete-state observation.
- [x] Re-run the original full parallel CUDA suite and focused repetitions; preserve actual GPU parity/memory-check evidence and source-only commit scope.
- [x] If unable to reach a tight red signal or safely prove a correction, report the unresolved risk honestly rather than marking a guessed fix successful.

## Scope

Camera/rig settlement and test synchronization only. No capture feature implementation, renderer changes, generic architecture work, user preferences/display modifications, global helper changes, remote publication, or raw git.

## Comments

The user is unavailable for manual testing and explicitly requested strongest reliability. Work autonomously on facts, but do not invent a causal conclusion from clean samples. The flaky detector's 0/10 sample does not imply a <10% 95%-confidence failure-rate bound: even under independent Bernoulli assumptions the one-sided bound is about25.9%; the full-suite condition is different anyway.

### 2026-10-03 iteration — resolved
Copied this read-only source ticket into the CUDA workspace and explicitly jj-tracked the ignored record; no separate spec/addenda, blockers, dependents or retry WIP were supplied. Preserved parent GPU-test commit 17ee63e9 and the broad-editing bookmark.
Phased real protocol delivery reproduced exactly 13.0 versus 16.2271003723 (23 assertions / 1 case, one failure), then the old position-only predicate failed the narrow regression before correction. Ranked falsifiable hypotheses and probes are in `../ORBIT-DIAGNOSIS.md`.
Changed only `src/tests/test_StudioRemoteE2E.cpp`: owning-thread observer publishes mutex-protected deep snapshots on ordered metadata receipts; settlement checks position/direction/up, with all original numeric comparisons intact. No production API/logic change, live cross-thread orbit reads, sleeps or scheduling/tolerance workaround remains.
Unrelated metadata preserves the completed centre 13.0; a reverted production negative control that re-applied stale orbit metadata failed 3.0 versus 13.0, proving genuine overwrite remains detectable. The tiny rounded earlier failure's exact interleaving is not proven; its invalid concurrent/early observation is removed. Finite passes do not prove zero risk; other scenarios' live reads remain outside scope.
Verified (all commands prefixed `cd /home/tarcila/Code/ANARI/vela/broad-editing-cuda &&`):
- `cmake --build _out/_cuda-check -j4 && cmake --install _out/_cuda-check` → passed, existing CUDA Release cache/install reused.
- `clang-format --dry-run --Werror src/tests/test_StudioRemoteE2E.cpp` → exit 0; `bash -n .scratch/reliability-verification/orbit-phased.sh .scratch/reliability-verification/verify-orbit.sh` → exit 0.
- `bash .scratch/reliability-verification/orbit-phased.sh` → red before correction, green after (39 assertions / 1 case); `seq 1 20 | xargs -P4 -I{} bash .scratch/reliability-verification/orbit-phased.sh` → 20/20, each 39 assertions / 1 case.
- `seq 1 10 | xargs -P4 -I{} bash .scratch/reliability-verification/orbit-repro.sh` → 10/10, each 103 assertions / 1 case (all four THEN paths); actual output counts checked, not merely helper exit status.
- `bash .scratch/reliability-verification/verify-orbit.sh full` → original `ctest --test-dir _out/_cuda-check -j4 --output-on-failure` passed 68/68 (21.99 s).
- `bash .scratch/reliability-verification/verify-orbit.sh full-repeat` → same full -j4 suite, `--repeat until-fail:3`, passed all 204 executions (55.76 s), counted per suite.
- `bash .scratch/reliability-verification/verify-orbit.sh heavy-repeat` → StudioRemote/Server/Client/TestClient `--repeat until-fail:10 -j4`, passed all 40 executions (185.51 s), counted per suite.
- `bash .scratch/reliability-verification/verify-orbit.sh gpu` → actual `[CUDA],[Downsample]` parity and compute-sanitizer memcheck each passed 98,792 assertions / 3 cases; 0 errors, 0 bytes leaked. Prior GPU tests/evidence untouched.
Logs stay in this workspace's `.scratch/reliability-verification`; source-only reproduction scripts/diagnosis are retained with this record. Root's pre-existing empty `sentinel-dbg-wrapper.stderr` was relocated into that scratch directory, not included in the commit. No global helper/user config/spec changes or publication.
Holes filed: none
