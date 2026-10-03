# Post-delivery reliability verification

Status: completed; next camera/capture increment remains a draft, not code.
Source merge: 6520efdf on bookmark broad-editing.

## CUDA: actual build and execution

The main workspace now has VSR_USE_CUDA=ON in _out/_cmake, with the full
UI/network/Lua/SDL3 configuration. Release build/install succeeded with four
jobs, default all-major architectures, nvcc12.9.86, automatically selected
CUDA host GCC14.4.0, C++ GCC15.3.0, and no compiler workaround.
Device: NVIDIA RTX5880Ada, capability8.9, driver615.71.09.

- Main parallel CTest:68/68.
- Main repeat-three parallel CTest:204/204 executions.
- Actual CUDA/downsample tests:98,792 assertions/3cases.
- Main Compute Sanitizer memcheck:0errors,0bytes leaked.
- Worker memcheck/initcheck/racecheck/synccheck all clean.
- Nsight independently recorded35kernel launches (fill, conversion, tone map,
  and multi-pass luminance reduction). Hidden-device negative control fails.
- Main headless/real-MCP chess:8,899 assertions/2cases; MCP selection:44cases.

New public CUDA/CPU parity tests compile out when CUDA is disabled, but require
a real device when run. docs/cuda-validation.md documents GPU-host validation
and the existing CI limitation: GitHub CUDA jobs are compile-only, not runtime
GPU proof. No GPU runner or remote CI configuration was provisioned.

## Host sanitizers and regressions

ASan+UBSan+explicit float-cast-overflow ran with leak checking ON, halt-on-error,
no blanket suppression, isolated CPU/headless configuration and install.

- Found and fixed one production bug: JSON numeric serialization converted a
  large finite double to long long before range checking. The guard now
  precedes the cast; real numeric response regressions pass.
- Fixed an invalid regression-test optional dereference after the engine
  correctly rejected queued work before the edit callback.
- Added48fixed-seed affine/parent-child/mixed edit-and-restore iterations with
  independent scalar oracles;64matrix cell/value boundary cases; thin protocol
  numeric validation and successful subsequent edit/restore checks.
- Instrumented CTest:36/36; core144cases; integration263cases;20stresspasses,
  with no final ASan/UBSan/float-cast/LSan finding. Parent independently reran
  the new4cases successfully. External prebuilt device internals are not
  themselves instrumented, and ASan is not a concurrency proof.

## Remote orbit test: proved cause, not guessed production change

Earlier parallel CTest failed orbit comparisons twice. Subsequent10focused
parallel cases and40heavy-suite executions alone passing did not erase that
observation. A controlled phased update reproduced16.2271versus13.0: the old
position-only predicate retained a partial rig before direction/up settled.
The scenario also read mutable server state from the test thread, contrary to
its fixture's ownership contract.

Only the test changed: ordered protocol receipts publish owning-thread deep
snapshots, and settlement checks the complete pose. Original tolerances remain.
A reverted stale-metadata production negative control still failed, proving
that a genuine overwrite is not hidden by the stronger test.

Post-fix original parallel suite68/68, repeat-three204/204, heavy repetitions
40/40, orbit10parallel runs, and phased20parallel runs all passed. The exact
interleaving behind the earlier tiny mismatch was not separately proven; its
invalid observation was removed. Other scenarios' live reads remain outside
this narrowly scoped correction.

The installed flaky wrapper also initially quoted bash -c incorrectly and
reported jobs that never executed the test. The local diagnostic validated
actual case/assertion counts and used a scratch-only corrected runner; global
host tooling was not modified. Zero failures in10samples is not a <10% bound
at95% confidence (under independent Bernoulli assumptions the one-sided bound
is about25.9%, and full-suite conditions differ).

## Next increment

.scratch/camera-capture/proposal.md is a retained, parent-corrected draft.
The initial planner failed before its final report; a focused advisor decision
supports qualified shared-device/index capture with capture-owned resources.
No capture code, new camera authority, server filesystem access, or new
implementation tickets are approved by this record.

MAX_STREAM_EVENTS=100K concerns Pi subagent output, not CUDA streams/events.
CUDA algorithms are verified; GPU ANARI capture/GUI interop and the proposed
capture lifecycle are not certified by those kernel tests.

## Evidence and local-only boundary

Selected ASCII GPU/sanitizer evidence is retained in evidence/cuda and
evidence/asan. Main configuration/build/runtime logs are main-*. The diagnostic
and source-only reproductions are ORBIT-DIAGNOSIS.md and accompanying scripts.
No private descriptors, credentials, Xauthority, binary profiling report or
runtime HOME is included in source commits. User config and screenshot remain
untouched; no remote publication or push. A final read-only review approved the
orbit correction after checking FIFO receipt ordering, owning-thread snapshot
publication, complete-pose settlement, lifetime/destruction ordering, timeout
handling, and the reverted negative control. CUDA/sanitizer verification caches
remain in their isolated workspaces for further backend qualification.
