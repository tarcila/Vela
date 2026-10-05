# Ticket 09 c2 — Helide/VisRTX qualification and completion (addendum03)

Scope: addendum03. No barney search/test/driver call in this sitting; archived
barney diagnostics (`[.world-bounds-qualification]`,
`[.world-bounds-natural-qualification]`) remain hidden and were not executed.
Handoff: `../barney-world-bounds-handoff.md`. Ticket 10 is user-scoped wontfix
in addendum03's sense — NOT fixed, barney stays `unavailable/unqualified_source`.

Base: integration bookmark `viewport-integration` = `f716779831be3cb5885259422719fbd454754bc9`
is an ancestor; owned stack rlxkruzu(82e988e7) → spttnslp → xyynrnpk(ddbe4457)
→ xwuqouuk(ade8352e) → this commit. Local generated `.envrc` preserved
uncommitted, SHA256 `30f1ba6e4677643f80cd4ac718365a02bd632ff3778e954116c423b2819b9e74`
before and after.

## Source change

- `AnariSceneRenderPass::hasCompletedFrame()`: true once the pass copied a
  naturally completed frame since setup; reset by cleanup. Asking starts nothing.
- `Viewport::worldBoundsSource()`: admits default-subtype `helide` and `visrtx`
  only; barney/others `unqualified_source`. Qualified source is
  `pending/source_transition` until its own first naturally completed frame, so
  backend cold init stays with natural rendering, not the inspection call.
- Tests: shared `naturalWorldQualification` probe; new hidden
  `[.world-bounds-hv-qualification]` (Helide/VisRTX only, scalar/array ×
  cold/post-frame, plus raw empty-world recording); new default host tests:
  source replacement/history/no re-query, retirement during driver call,
  deadline/cancel during driver call, deadline-before-execution never invokes
  provider, partial binding never observed.
- MCP README: qualified set, readiness, empty-world disposition.

## Real-driver qualification (RED-before: production VisRTX was `unqualified_source`)

Library resolution: process loads `/nix/store/2im1s42z3ij23ib3rrbsdkblmmp37sjg-visrtx-tsd_removed-unstable-2026-09-30/lib/libanari_library_visrtx.so`
(SHA256 `064ac5ed…7c34a`, via LD_DEBUG and `/proc/<pid>/maps`) and
`/nix/store/sjapb5vn…-anari-helide-0.16.0-unstable-2026-09-28/lib/libanari_library_helide.so`.
GPU: NVIDIA RTX 5880 Ada. No env forcing of a different device.

`vsrIntegrationTests '[.world-bounds-hv-qualification]'` → **2 cases / 454
assertions PASS** (twice: logs `09-c2-hv-qualification.log`, `-final.log`).
20 property calls, all success; literal [2,3,4]–[4,5,6] then [49,3,4]–[51,5,6]
for both drivers, both shapes, cold and post-natural-frame first query.
VisRTX post-edit reports ±2e-5 float padding (Approx-equal). Max property call
0.895 ms (cold, pre-render); max post-render 0.002 ms. 16 ordinary frames, all
nonbackground (VisRTX first frame 273 ms — natural render cost, not query).

Empty world: Helide success with min=+inf/max=-inf; VisRTX success with
±FLT_MAX reversed. No documented empty convention → host publishes
`unavailable/invalid_bounds`, never `empty` (test asserts validation rejects).

## Native production (actual vsrViewer, private Xvfb/XAUTHORITY/TMPDIR, Pi 1.0.2 MCP)

`python3 .scratch/viewport-integration/09-c2-production-viewer.py {helide,visrtx}`
with glTF literal triangle (importer transform node), ordinary `vsr_edit`
local position [50,0,0]. Proofs `09-c2-production-{helide,visrtx}-proof.json`.

| | Helide | VisRTX |
|---|---|---|
| before edit | [-2,-3,-4]–[5,6,7] ×6 | same (±2e-5) ×6 |
| after edit | [48,-3,-4]–[55,6,7] ×6 | same (±2e-5) ×6 |
| stale observations after edit | 0 | 0 |
| driver calls / max ms / status | 17 / 6.08 / 1 | 17 / 3.04 / 1 |
| SDL present heartbeats / max gap | 64 / 16.8 ms | 62 / 16.8 ms |
| source identity | primary viewport, ≠ AgentView | same |

Camera unchanged, historical replay returns original box, no render inside
bounds call (LD_PRELOAD oracle), one Scene write (the edit). Pending was 0 in
both runs (frames already completed by first query); pending path is covered
by code inspection only, not natively observed.

## Regression / verification (own warm RelWithDebInfo tree, j3, then install)

- build+install: PASS (`09-c2-build.log`).
- `[world-bounds]`: 4 cases / 206 assertions PASS.
- focused `[bounds],[first-observation],[node-edit],[view-identity],[request-guidance]`: **49 cases / 15183 assertions PASS**.
- full `vsrIntegrationTests`: **376 cases / 136463 assertions PASS** (old 374 + 2 new default cases).
- `ctest -C RelWithDebInfo -j1`: **68/68 PASS**, 72.5 s.
- CPU Memcheck `[bounds]`: 11 cases / 389 assertions, 0 errors, 0 definite/indirect/possible, 2350 B reachable. Excludes real drivers/GUI/GPU.
- clang-format --dry-run --Werror on 4 changed source files: PASS.
- Test exe SHA256 `c6cfa4cc…3f32`; vsrViewer `c3f06671…b366`.

## Limits

ANARI_WAIT remains uninterruptible; measured latencies are observed on this
host/driver build only, not a universal guarantee. Five-second deadline/caps
unchanged. No GPU sanitizer. Coverage is viewport_world, not Scene
certification. Final independent whole-spec review and runtime acceptance are
outer gates, pending.
