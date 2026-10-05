# Handoff — barney transform-array world bounds

## Disposition

The user stopped barney investigation and explicitly removed it from this work's qualification scope. Continue usable bounds with Helide and VisRTX only. Keep barney bounds unqualified; do not fix its driver, substitute a CPU scanner, alter rendering or claim the wrong box is usable. Any future barney work needs separate authorization. User policy also says to document other device-specific issues and move on unless they block usable/safe host behavior.

## Reproduced failure

Existing ANARI world property: `bounds`, type `ANARI_FLOAT32_BOX3`, queried with `ANARI_WAIT`, matching vsrViewer Frame All. A radius-1 sphere at the origin with an instance translation(3,4,5) should yield literal world[2,3,4]–[4,5,6]. After a notified translation edit to(50,4,5), expected[49,3,4]–[51,5,6].

Helide returns both boxes for scalar and one-element transform-array instances. Installed barney returns correct scalar boxes, but transform-array worlds return[-1,-1,-1]–[1,1,1], even after completed ordinary renders and notified edits. Fresh worlds whose FIRST bounds query follows natural rendering fail the array case too. Query status is success, not a missing-property failure. Do not treat natural rendering or fast warm query times as correction.

Original no-render/cold array diagnostic:29failures, first blocking property call5237.357ms. Later lifecycle-controlled scalar/array diagnostic:20property calls,16completed nonblank frames,448assertions/30failures; five wrong array boxes account for30coordinate failures. The old process-cold latency remains observed historical evidence, not a universal current timing claim. The later probe's maximum post-render call was0.006436ms—a fast wrong answer.

## Exact evidence and reproduction

Owned preserved workspace: `/home/tarcila/Code/ANARI/vela/viewport-world-bounds`. Full WIP source/proof chain: `rlxkruzu` then continuation `xyynrnpkryur`; integration source was NOT landed when this handoff was written. Do not drop this stack while continuing Helide/VisRTX.

Pointers (avoid duplicating their detailed commands/logs):
- `.scratch/viewport-integration/issues/10-barney-world-property-qualification.md` in the worker: original qualification blocker.
- `.scratch/viewport-integration/09-verification.md`: cold experiment and partial Helide/native provider proof.
- `.scratch/viewport-integration/09-c1-verification.md`: scalar/array and pre-query/post-natural-render minimization.
- `.scratch/viewport-integration/09-c1-natural-proof.json` and `logs/09-c1-natural-probe.log`: raw external evidence.
- `src/tests/integration/test_IntegrationInspection.cpp`: opt-in `[.world-bounds-qualification]` and `[.world-bounds-natural-qualification]` diagnostics. They are retained failing diagnostics, not passing default-suite qualification. Future barney agents can run them only under separately authorized scope.

Those tests use actual ANARI devices/Scene/RenderIndex/provider operations and independently expected literals. Producer render/map occurs only as explicit fixture/natural production, never inside the bounds callback. No internal backend mechanism or rendered placement parity was established by nonblank frame counts.

## Installed source provenance and limited source inspection

Installed package observed during investigation: `/nix/store/l40k5sr8nqydlqggr9sxkxvnva1m08ry-barney-0-unstable-2026-09-25`; libraries include `libanari_library_barney.so` and `_optix.so`. Package derivation identifies upstream `NVIDIA/barney` revision `a2f7b18a4e6066f654d2375a7907cf88cea22a8a`; its original source store path was absent. Read-only fetching recovered selected exact-revision files. No external driver was edited or rebuilt.

- https://github.com/NVIDIA/barney/blob/a2f7b18a4e6066f654d2375a7907cf88cea22a8a/barney/anari/World.cpp
- https://github.com/NVIDIA/barney/blob/a2f7b18a4e6066f654d2375a7907cf88cea22a8a/barney/anari/Instance.cpp
- https://github.com/NVIDIA/barney/blob/a2f7b18a4e6066f654d2375a7907cf88cea22a8a/barney/anari/Instance.h

Source observations, NOT a repaired/runtime-proven internal diagnosis: World::getProperty flushes commits and calls makeCurrent() under WAIT before unioning instance bounds. Instance::commitParameters reads only scalar FLOAT32_MAT4 `transform`, and Instance::bounds transforms only the lower/upper group-box corners. These warrant checking declared transform-array extension support and affine-corner correctness in future driver work; do not claim they prove the entire failure mechanism or an independently reproduced rotation/shear defect. The user's stop instruction arrived immediately after these source reads; no further barney search/probe is authorized in the current task.

## Suggested skills for a future separately authorized driver session

`diagnosing-bugs` for the existing deterministic RED lifecycle diagnostic; `tdd` for the genuine ANARI boundary regression; `code-review` for any explicitly authorized driver patch. Read actual backend repository instructions before modifying it. Keep driver/runtime version provenance and source classification explicit.

## Current integration work

Usable bounds follows the user's selected `viewport_world` semantics, not certified coverage of every VSR Scene edit. Helide/VisRTX qualification and host interface/lifecycle tests continue under a new additive scope artifact; this barney handoff is not a request to change that implementation, frozen contracts or historical trackers. No remote issue/PR/push, credentials or user Scene/config mutation occurred.
