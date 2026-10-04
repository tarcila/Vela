# Ticket 01 verification — 2026-10-04

## Frozen inputs and source boundary

- Worker parent and integration bookmark both verified as
  `ac9f90300553507e5902a6893b18ecce95a2abf5`, before implementation and again
  immediately before recording. No reset/rebase/bookmark move.
- Spec SHA256 remains
  `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
  No addenda. Only ticket 01, its records, and its inspection/MCP source/tests
  changed. Other tickets, historical parents, encoder/caps, configuration,
  other workspaces, and the user's viewer/controller were not edited.
- Existing identity/inspection/edit/restore/operation machinery is reused;
  public node omission and wire-token edit/restore fixtures verify the inherited
  integration base rather than assuming a bookmark means those behaviors exist.
- `RequestDiagnostics.hpp` owns closed field/reason enums and fixed 16-entry
  storage. Inspector's canonical filter validation now reports those errors on
  native `Built`; the carrier adds JSON type/requiredness checks, without live
  identity lookup. Handle-kind validation uses only encoded syntax. Live handle
  validity and current authority retain their existing admitted safe-point path.
- MCP emits only known fields/reasons and recognized kind (otherwise `unknown`),
  with a 4096-byte diagnostic JSON ceiling. No input value is emitted. Required
  fields, optional omissions, meaningful empty names/substrings/cursors,
  intersecting filters, and legacy unknown properties remain compatible.
- Tool descriptions deliver minimal object/layer/node examples and literal
  RootDelta/BodyTop inheritance arithmetic. Siblings are independent roots;
  no common-parent or piece-name inference, new tool, adapter rewrite, or
  discriminated-union/provider assumption was introduced.

## Public TDD and final verification

Actual behavioral red observations are retained in `logs/01-red-test.txt`
(missing mixed-kind details), `01-guidance-red-test.txt` (missing descriptions),
`01-cursor-red-test.txt` (missing cursor details), and
`01-default-kind-red-test.txt` (omitted kind diagnostic). Subsequent public
regressions passed. Native/required-ID test attempts first hit a compilation
error in the native Status assertion (`01-native-red-build.txt` and
`01-id-red-build.txt`); these are not claimed as behavioral red tests.

All runtime commands used:
`LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH"` and
`TMPDIR="$PWD/.scratch/viewport-integration/tmp"`.
Build/install preceded verification. Full tests were serialized.

| Command (workspace root) | Final result | Raw log under `logs/` |
|---|---|---|
| `cmake --build _out/_cmake --config RelWithDebInfo -j6` | exit 0 | `01-final-build.txt` |
| `cmake --install _out/_cmake --config RelWithDebInfo` | exit 0 | `01-final-install.txt` |
| `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[request-guidance]'` | 551 assertions, 6 cases | `01-final-focused.txt` |
| same binary `'*MCP node queries parse filters and expose placement records*'` | 97 assertions, 1 case | `01-native-omission.txt` |
| same binary `'*MCP node enabled edit inspect restore uses only wire tokens*'` | 133 assertions, 1 case | `01-wire-edit-restore.txt` |
| `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` | 68/68 passed, 69.26 seconds | `01-full-ctest.txt` |
| integration binary `'[integration]'` | 117985 assertions, 317 cases | `01-full-integration.txt` |
| `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --log-file=.scratch/viewport-integration/logs/01-valgrind.txt _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[request-guidance],[node-edit],[node-position]'` | exit 0; 7362 assertions, 31 cases; 0 errors, 0 definite/indirect lost bytes | `01-valgrind.txt`, `01-valgrind-tests.txt` |
| `clang-format --dry-run --Werror` on all seven changed C++ files | exit 0 | `01-format.txt` |

The full integration assertion total varies with transport/safe-point polling;
this is the final observed total, not a fixed expected total. Early exact Catch
scenario selectors omitted the `Scenario:` prefix and matched nothing. Those
zero-match attempts are excluded; corrected wildcard selectors positively
matched the 97/133 fixtures above.

New regressions cover descriptions/schema requiredness, native/MCP diagnostics,
all known optional field types, required ID types, placeholder handle rejection,
malformed cursor privacy, omitted-kind reporting, 16-entry/byte bounds,
unknown-property compatibility, exact empty filters for all three kinds, and
same-request-ID correction after pre-admission rejection. Existing intersecting
node handle/layer/parent filters and versioned inspect/edit/reinspect/restore
remain positively tested.

## Actual installed Pi client and real isolated viewer

`python .scratch/viewport-integration/01-live-viewer.py` passed three consecutive
final runs (`01-live-run.txt`, `01-live-repeat.txt`, `01-live-repeat2.txt`). It
starts only its own Xvfb and production `vsrViewer --agentIntegration -gltf`
with `/data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf`.
Runtime/config/TMPDIR are workspace-local; its X server uses a private
MIT-MAGIC-COOKIE cookie, and only its own processes are terminated. The fixture
holds an X connection through startup to avoid X-server reset racing SDL's
availability probe; early startup failures are not counted as successful runs.
No user's Scene/camera/device/configuration/restart/controller was touched.

`01-live-client.mjs` imports the installed Pi `McpServerConnection`,
`createDefaultTransport`, and `createMcpToolDefinition`, using the workspace's
`vsrMcpProxy`. It captures actual tools/call messages at the stdio transport
boundary and executes the real exposed tool definitions. No substitute schema
builder or mock client is used. `01-live-proof.json` records:

- Native tools/list and Pi's model-facing ToolDefinition both require exactly
  `request_id, scene_id`; descriptions arrive intact.
- Actual Queen_B request has only request_id/scene_id/kind/name.
- Actual pawn request has only request_id/scene_id/kind/name_contains/node_kind.
- Fresh session discovery followed by **1 Queen_B transform**, **32 pawn body/top
  transform nodes**, and **1 fresh-handle Queen_B reinspection** with unchanged
  node edit version. No object filters, cursor, placeholders, or nullable rewrite
  were added. No Scene edit occurred.

Final executable SHA256 values:
- viewer: `83bfe0440fef79bf448c3acaef2a560e146b596139157868f9683cc79a60ee27`
- proxy: `475da5bada099fb06e7c74da2fa76912599f363200a2ca9c1a1ed957a6cf868c`

The existing configuration kept CUDA, interactive UI, networking, Lua,
integration/MCP and SDL3 enabled. Libraries were installed in this workspace;
this workspace's installed library directory was first in runtime lookup.

## Honest remaining limits

Valgrind reported 2376 possibly-lost bytes in 15 blocks and 4186630 reachable
bytes in 180 blocks, plus an unhandled driver ioctl and large-address-range
warning. No suppressions were used. This is relevant host parsing/inspection/
edit/restore Memcheck evidence, not a whole-process leak-free or GPU-memory
qualification claim. No sanitizer build was substituted for the warm enabled
build. Some unchanged suites still use their own hardcoded temporary fixtures;
full runs were serialized to avoid collisions.

The live fixture exercises the installed Pi definition/client/stdio/native
boundary, not a remote model-generated request or an external/custom provider
or gateway rewrite. No omission loss was demonstrated, so no Pi/backend defect
or client-adapter fix is claimed. Read-only helide fixture inspection does not
qualify barney/CUDA viewport images, presented frames, responsiveness, or any
later ticket. No live edits on the user's Scene were authorized/performed.
Final independent whole-spec correctness/Standards review is **pending**, to be
provided by the integration orchestrator after merges; author inspection here
is not called independent review. No holes or split tickets were filed.
