# SAME09 continuation — naturally rendered WORLD qualification

## Disposition

**ready-for-human, still blocked by existing hole 10.** No new tickets, no production source enablement, no driver changes, no CPU fallback. The scalar-vs-array minimization provides stronger external evidence than the original cold-array experiment, but does not establish an internal backend cache/build cause. Final whole-spec independent review remains an outer-orchestrator gate.

Workspace: `/home/tarcila/Code/ANARI/vela/viewport-world-bounds`. Dispatch working change `xyynrnpk` was clean and empty, parent `40af9b1c`, preserving owned WIP `rlxkruzu/96aad43d`. Recorded integration base/bookmark `57765ec19da2a6b9a69c6379d20d2327fee01578` was confirmed an ancestor before source changes and remains unchanged. No reset/drop/bookmark/user config/viewer/controller/Scene edits.

## One real external lifecycle probe

Added hidden opt-in diagnostic `[.world-bounds-natural-qualification]` in `src/tests/integration/test_IntegrationInspection.cpp`. It exercises the existing public real-Scene/provider/operation seam and raw shared RenderIndex helper, not a replacement property implementation. Original hidden cold diagnostic and all its 29-failure evidence remain untouched. Existing partial provider/shared helper/production Helide/native proof remain intact.

Eight combinations: actual default Helide/barney × scalar/one-element transform array × with/without a property query before rendering. Each private Scene contains one radius-1 sphere at the origin, instanced at `(3,4,5)`. The literal expected WORLD box is `[2,3,4]–[4,5,6]`. Sole fixture `populate()` occurs before inspection. The ordinary producer creates a fixed camera/renderer/frame, completes `anari::render` + `anari::wait`, and maps its actual float color output. A normal notified scalar transform or transform-array data edit moves the sphere to `(50,4,5)`; the same camera/renderer/frame completes another ordinary frame, then inspection expects `[49,3,4]–[51,5,6]`.

The `cold_query=false` cases use fresh Scenes/worlds whose **first property query occurs only after the first completed ordinary frame**. They have no prior Frame All/query. This separates earlier-query initialization from natural rendering. Normal fixture production is explicit and separate from the provider callback; it is not inspection-induced rendering. The provider creates no device/camera/renderer, resets nothing, calls no populate/render/map, and checks that the fixture render counter is unchanged. Frame output maps belong only to producer validation, not bounds inspection. Source lifetime generation remains 1 because these are ordinary edits to the same world, not source replacement; replacement qualification remains pending.

`09-c1-natural-proof.py` summarizes the single raw run without launching anything. `09-c1-natural-proof.json` retains every property boolean/value/time, source generation, phase, producer count, owner-thread before/after heartbeat tick and actual nonbackground output count. Owner heartbeat gaps are measured synchronous property-call gaps, **not independent SDL/GUI heartbeat qualification**.

Command (own installed libraries first, absolute workspace-local TMPDIR):

`LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" TMPDIR="$PWD/.scratch/viewport-integration/tmp" _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[.world-bounds-natural-qualification]'`

Result: **exit 30; 1 test case, 448 assertions, 30 failed**. This is deliberately failing acceptance evidence, not a default regression pass. One earlier compile failed on a ForestNode accessor typo, fixed before the actual probe; it is not behavioral RED. The observed wrong-box failure remains RED; no GREEN or feature completion is claimed.

| Actual device/shape | After ordinary frame | After notified edit + frame |
|---|---|---|
| Helide scalar | literal correct | literal correct |
| Helide array | literal correct | literal correct |
| Barney scalar | literal correct | literal correct |
| Barney array | **[-1,-1,-1]–[1,1,1]** | **[-1,-1,-1]–[1,1,1]** |

Both query-history variants yield the same table. All 20 raw property calls returned success; five barney-array calls returned wrong boxes (four after rendering plus one before). Each wrong box fails six independent coordinate assertions: **30 failures**. All 16 ordinary frames completed with nonbackground pixels: Helide 10; barney scalar 31 initially/16 after edit; barney array 28 before/after. Those output counts establish real nonblank frame production, not pixel/geometry parity or a proof of correct rendered placement.

Max observed property time across this run: **18.1303 ms** (barney scalar before rendering). Max post-render property time: **0.006436 ms** (barney array). Helide max post-render: **0.001838 ms**. These fast wrong answers do not qualify barney. The old first-call 5237.357 ms remains valid historical cold evidence; this run does not reproduce or disprove process-cold latency and does not prove safe five-second completion across arbitrary worlds. ANARI_WAIT remains noninterruptible. No NO_WAIT substitution or guessed internal cause.

Measured executable SHA256: `69c83f980cf2a20b6103b52910573e0d939a154725d400de20f20c26141e5e2e`.
Raw probe log SHA256: `fd0f83c4f9dbee70866eb2276d8484ed879c75be507a33b2b4128976d3c97730`.
After that one diagnostic run, exception-safe device/frame cleanup guards were added. No driver query/producer logic changed and the probe was not retried to green. Final build/default regressions below use the cleanup-guard source; direct external evidence is explicitly from the earlier measured executable.

## Verified final partial source

Own warm enabled Ninja Multi-Config tree, own install prefix and CMAKE_HOME_DIRECTORY checked. CUDA/UI/apps/networking/Lua/SDL3/integration/MCP stay enabled. No configure or foreign tree/output/config changes.

- `cmake --build _out/_cmake --config RelWithDebInfo -j3` + `cmake --install _out/_cmake --config RelWithDebInfo`: PASS before runtime verification. Final logs `09-c1-final-build.log`, `09-c1-final-install.log`.
- `vsrIntegrationTests '[bounds],[first-observation],[node-edit],[view-identity],[request-guidance]'`: **47 cases / 15117 assertions**, PASS.
- `vsrIntegrationTests`: **374 cases / 136095 assertions**, PASS. Runtime-loop assertion counts vary; both hidden qualification diagnostics excluded, not absent-match claimed as pass.
- `ctest --test-dir _out/_cmake -C RelWithDebInfo --output-on-failure -j1`: **68/68**, PASS, 72.74 s. Full suites serialized.
- Owned CPU `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect vsrIntegrationTests '[bounds]'`: **9 cases / 323 assertions**, PASS; 0 errors, 0 definite/indirect/possible lost bytes, 2350 bytes reachable in 8 blocks. This excludes external drivers/GUI/GPU and the new natural-render probe; not clean GPU/GUI/instrumentation qualification.
- `clang-format --dry-run --Werror src/tests/integration/test_IntegrationInspection.cpp`: PASS, exact one changed source file. Existing broader formatting proof remains historical.
- `python .scratch/viewport-integration/09-c1-natural-proof.py`: PASS; parses 20 calls/16 frames/5 wrong calls from the retained failing run. No driver work.

Runtime environment for all final tests: `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" TMPDIR="$PWD/.scratch/viewport-integration/tmp"`.
Final source file SHA256: `8f2413013ebc7b306430f5ab372c5c906d2432db47343bd6ef936634ac0fd624`.
Final test executable SHA256: `fc20c6fedfc5e4d8ef21039920e14fd0ff6b68f2683350a4553674f638b323a9`.

## Remaining limits

Hole 10 remains load-bearing: natural rendering does not repair barney's transform-array WORLD property. Scalar-only success cannot justify blanket enablement, even if a particular chess Scene uses scalars. No supported-shape policy/waiver or external fix is invented. Production barney remains unavailable/unqualified_source, and its existing guarded native proof is not re-labelled property qualification. Prior production Helide proof remains preserved, not rerun/extended here.

No new production two-device ordinary-edit qualification or user-instance query is claimed. Current-read/provider/publication/retrieval transitions, source visibility/retirement/replacement/current-device and cancellation/deadline metadata/timestamp/replay/old-source tests still require the fuller acceptance recorded in original 09 verification. Existing partial negative and MCP regressions remain passing but do not supply missing real-world criteria. No standalone helper-only ticket or review-route hole was filed. Stop now on the actual external blocker, rather than spending the remaining budget implementing unsafe exposure.
