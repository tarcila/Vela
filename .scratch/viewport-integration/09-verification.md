# Ticket 09 — blocked partial WORLD provider increment

## Outcome and immutable base

**ready-for-human**, not resolved. Hole 10 is load-bearing (`contradicts`): current barney did not meet the required transformed-world correctness / finite servicing qualification. No CPU scanner, renderer workaround, deadline increase, NO_WAIT switch, async worker, external driver edit or exact-current-Scene coverage tracking was introduced. Final whole-spec independent review is pending with the outer orchestrator, not supplied by author inspection.

Workspace `/home/tarcila/Code/ANARI/vela/viewport-world-bounds`; clean empty worker `rlxkruzu` at dispatch. Parent and integration bookmark both `57765ec19da2a6b9a69c6379d20d2327fee01578`, rechecked before final recording. No reset, bookmark move, foreign mutable build tree or user viewer/controller/config changes.

Immutable SHA256 values (unchanged at final recording):
- spec: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`
- addendum 01: `62fae22eb49870782ffe14f9d84acfeb7672741c8e5bf96b1ff79d52323f999b`
- addendum 02: `a183a037f9a6d3e83f8d0c293ac3bd472ad8e14d7bc5f007ba385747fe20c7ad`

## Partial source

Public optional SceneRegistration / ContextSceneOptions WORLD provider defaults unbound. The driver callback runs only from authorized owner-thread service; a separate cached current-source check prevents publishing a retired identity/generation. Raw RenderIndex `queryWorldBounds(out)` uses the same ANARI_FLOAT32_BOX3/ANARI_WAIT as Frame All, preserving status/output on failure. UI warning and framing fallback remain unchanged. Bounds never uses the synthetic framing box.

The production adapter chooses the first live visible registered slot in fixed UI order (Viewport, then Secondary View), never independent AgentView. Weak lifetime guards and registered view identity precede dereference; cached device/source checks precede publication. Available returns finite ordered min/max, viewport_world coverage, bounded opaque source identity/generation and actual Device Identifier/Subtype. Covered Scene revision is null; current Scene/inspection tokens explicitly describe observation only. Existing world filtering and external instances are reported as the selection basis. No view name/camera/pointer/credential is in bounds source metadata.

Production barney and unknown Device Identifiers/Subtypes are unavailable/unqualified_source: no inspection driver call. Partial Helide support is shown below, not a completed two-device feature or a universal latency guarantee. No production empty convention is asserted; arbitrary nonfinite/reversed results are unavailable/invalid_bounds. Explicit provider EMPTY and PENDING states are separately tested.

Five-second queue-inclusive request policy, shared 8 MiB inspection scratch and existing operation history remain. Post-driver abandonment checks suppress late publication; **they do not interrupt ANARI_WAIT**. Callback metadata has a 128-byte-per-token contract and is validated before bounded JSON/retention. No second result store/cache is added.

## TDD and real external failure

Approved seams: real Scene/public provider and admitted MCP, with real ANARI worlds and actual production viewport proof supplementing controllable external callbacks.

- `09-red.log`: useful-world literal provider test failed at the behavioral assertion: `unavailable != available` (1 case, 7 assertions, 1 failure), before makeBounds implementation. Earlier red build failed due to a test enum typo, fixed before this behavioral RED; not counted as TDD evidence.
- `09-green.log`: after implementation, the value tracer passed. Real Helide default WORLD returned [2,3,4]–[4,5,6] for a radius-1 sphere instanced at (3,4,5), and [49,3,4]–[51,5,6] after ordinary notified transform-array edit to (50,4,5). No frame/camera/renderer/render existed in this fixture. Maximum measured Helide property call: 3.263 ms.
- The same direct raw-helper/provider fixture with actual default barney returned [-1,-1,-1]–[1,1,1] before/after and repeatedly, not the expected transformed WORLD box. First ANARI_WAIT took **5237.357 ms**, exceeding the entire five-second allowance. This run was **2 cases / 294 assertions / 29 failures** (28 wrong-box, one latency). Those failures are retained, not called a pass. No particular internal GPU build mechanism or universal cold/warm behavior is claimed.
- The failing two-device qualification fixture is opt-in `[.world-bounds-qualification]` and is deliberately excluded from default regressions. It is an unmet acceptance diagnostic, not an absent-match pass or fake qualification. `vsrIntegrationTests '[.world-bounds-qualification]'` reproduces the independent literal expectations.

## Actual production native MCP proof (isolated fixtures)

New `09-production-viewer.py`, `09-production-client.mjs`, `09-production-oracle.cpp` preserve all old proof scripts. Each launch has private Xvfb/TCP/Xauthority, TMPDIR, config/runtime and controller. Own `_out/lib` is first. Actual own-tree vsrViewer imports a private OBJ with literal vertices (-2,-3,-4), (5,-3,-4), (-2,6,7). No user Scene/file/controller/display or persistent camera state is touched. Node 24.21.0 and Pi 1.0.2 ToolDefinition/McpServerConnection transport the native requests; no model/provider invents values. Read-only allowlist is discover/view_info/bounds/operation_status.

The diagnostic preload forwards all calls unchanged, observing raw WORLD-property values/timing, SDL submission heartbeats and frontend render calls nested inside bounds. It supplies no result values/pixels and performs no render/reset/readback. Ordinary viewer rendering is allowed to continue naturally.

- Helide: **12 available actual-primary-viewport observations**, literal min [-2,-3,-4], max [5,6,7], correct opaque identity (distinct from independent AgentView), device/subtype, observation-only revisions, unchanged actual camera pose and exact historical replay. Max measured WORLD call across startup UI plus native query trace on the final executable: 7.006 ms; max native bounds round trip 30.820 ms; 17.818 ms max SDL heartbeat gap. No frontend render nested in property query. `09-production-helide-proof.json`.
- Barney: **12 unavailable/unqualified_source observations**, no fabricated min/max/source, unchanged actual camera pose, historical replay and live SDL heartbeats. Max natural startup Frame All property call in this untransformed OBJ fixture: 49.303 ms; heartbeat gap 36.332 ms; native unavailable round trip 30.388 ms. These are **NOT barney inspection/property correctness or post-edit qualification**: bounds intentionally does not call its driver. `09-production-barney-proof.json`.
- Four initial private-display launches intermittently failed SDL initialization, including one retry on :193. No source workaround/features were changed. The proof runner now retains its authenticated X client through SDL startup, as the existing 04 runner does, instead of closing the last X client between readiness and viewer startup. Final :193 TCP/Xauthority runs passed on both devices; logs/proofs name those final runs. Failed temporary instances were terminated; no user X server was used.
- No production post-edit barney proof, Chess-private-fixture proof or universal GPU/driver latency claim. The direct real Helide fixture covers notified transform edits; native production proof here remains read-only.

## Verification and resource scope

Own existing warm Ninja Multi-Config RelWithDebInfo tree, install prefix/workspace CMAKE_HOME_DIRECTORY verified. CUDA, UI/apps, networking, Lua, SDL3, integration and MCP all remain ON; executable debug compression is nonsemantic. No configure/bootstrap/foreign cache deletion or feature disable. Build/install commands (j3) ran before every runtime pass:

`cmake --build _out/_cmake --config RelWithDebInfo -j3`
`cmake --install _out/_cmake --config RelWithDebInfo`

Every runtime invocation uses `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" TMPDIR="$PWD/.scratch/viewport-integration/tmp"`. Full tests serialized with CTest `-j1`. Logs are local ignored raw artifacts.

- Default `vsrIntegrationTests`: **374 cases / 136584 assertions**, final pass (earlier run 136045; runtime-loop assertion counts vary). Excludes hidden qualification diagnostics; accepted predecessor had 372 cases.
- `[bounds]`: **9 cases / 323 assertions**, pass. Includes literal provider success and nonfinite/reversed/oversized metadata/property failure/pending/explicit empty/retired generation/read-withdrawal-at-callback/publication/replay, alongside legacy unbound/canonical identity/cancel/queue timeout/current-read retrieval/history/shared-scratch exhaustion tests.
- `[bounds]~[world-bounds]`: **7 unchanged unbound cases / 183 assertions**, pass. This is the actual current count, not the dispatch's historical 161-assertion reference. No legacy assertion was removed or relaxed.
- `[bounds],[first-observation],[node-edit],[view-identity],[request-guidance]`: **47 cases / 15117 assertions**, pass (actual nonzero matches).
- `ctest --test-dir _out/_cmake -C RelWithDebInfo --output-on-failure -j1`: **68/68**, final pass (72.65 seconds; earlier run 72.64).
- Owned CPU `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect vsrIntegrationTests '[bounds]'`: pass, **9 cases / 323 assertions**, 0 errors, 0 definite/indirect/possible lost bytes, 2350 bytes reachable in 8 blocks. This excludes real driver and GUI/GPU fixtures: not a clean GUI/GPU/sanitizer claim. External-driver instrumentation remains unqualified.
- clang-format: 13 changed C++ files, checked with dry-run/Werror; unrelated historical viewer formatting restored, with its newly added provider block checked by explicit line range instead of formatting AgentView. README is documentation only.

## Remaining limits / next step

Resolve hole 10 before claiming ticket 09 complete. Remaining acceptance includes genuine barney transformed bounds and owner-thread latency/readiness, production ordinary-edit/query evidence on both devices, fuller source retirement/replacement/current-device tests, callback/source-check authority/deadline/cancel transitions, real empty/missing/unsupported-property external behavior and configuration qualification beyond demonstrated fixture scope. Existing regressions and controlled-provider negative tests do not certify those missing real-world cases. Independent whole-spec review is still the outer final gate.
