# Mergerdispatch7 — ticket06 scoped integration verification

Workspace: `/home/tarcila/Code/ANARI/vela/viewport-integration`.
Worker change: `wpopuxzqxrouqompnmyymxpxmsmpvyov` / `70a795672947bcfb9c013b0630aa2c4466688c81`.
Recorded and actual direct parent / initial integration bookmark: `723fa3a765c3906629e8a1acebe72b77c54f8e76`.
Accepted landed bookmark: `viewport-integration` now points to the unchanged worker commit `70a795672947bcfb9c013b0630aa2c4466688c81`.

## History, scope and authority

Initial integration working copy was empty/clean. Inspected actual worker diff (11 files, 1371 insertions/22 deletions), resolved ticket, worker outcome and complete verification. Its eight source/test/doc files are Translation.hpp, AgentEdit.hpp, EditTools.hpp, Activity.hpp, Operations.hpp, test_IntegrationEdit.cpp, test_IntegrationMcpTools.cpp and docs/live-placement-editing.md. The other three are its 06 ticket/outcome/verification records. No new issue records were omitted; worker reports none. Integration 01–05 ticket statuses are resolved; accepted 04 qualification evidence and limitations were inspected, not recreated as new GPU proof.

`jj new 70a795672947bcfb9c013b0630aa2c4466688c81` checked out an empty child for independent verification, leaving the bookmark on its recorded base. No divergence/rebase/merge conflict/source repair occurred. Explicit `jj log -r 'ancestors(@) & conflicts()' --no-graph` returned no conflicts. After all checks passed, clean working copy, exact old bookmark and exact worker parent were reconfirmed, then `jj bookmark set viewport-integration -r 70a795672947bcfb9c013b0630aa2c4466688c81` performed the fast-forward-equivalent landing. Original code-plus-ticket change retained intact. Final working copy clean and conflict state false; see `merge06-landed.md`.

Spec SHA-256 before/after: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`. No frozen spec/ticket acceptance/progress/executor/configuration/cap/encoder/global-store/source edits. No rawgit, push, subdelegation, user live Scene/controller mutation, other-workspace mutation or cleanup.

## Independent commands and actual counts

All commands below ran in the integration workspace. Before every runtime:

```
export LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}"
export TMPDIR="$PWD/.scratch/viewport-integration/tmp"
```

All proof logs are bounded Markdown files under the absolute directory `/home/tarcila/Code/ANARI/vela/viewport-integration/.scratch/viewport-integration/logs/`, with prefix `merge06`. These results are independently executed by this merger, not copied author results. Poll-dependent counts differ honestly from the author evidence.

| Command | Actual result | Log basename |
|---|---|---|
| `cmake --build _out/_cmake --config RelWithDebInfo -j3` | exit 0, warm configured enabled tree | merge06-build.md |
| `cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` | exit 0, before every runtime pass | merge06-install.md |
| `clang-format --dry-run --Werror src/vsr/integration/edit/Translation.hpp src/vsr/integration/edit/AgentEdit.hpp src/vsr/integration/mcp/tools/EditTools.hpp src/vsr/integration/activity/Activity.hpp src/vsr/integration/operations/Operations.hpp src/tests/integration/test_IntegrationEdit.cpp src/tests/integration/test_IntegrationMcpTools.cpp` | exit 0, seven changed C++ files | merge06-format.md |
| `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit]'` | exit 0, 2142 assertions / 9 cases | merge06-commit.md |
| `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit],[translation-preview]'` | exit 0, 3275 assertions / 18 cases | merge06-selection.md |
| `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit],[translation-preview],[node-edit],[node-position],[node-mcp],*restor*,*Canonical*'` | exit 0, 24621 assertions / 71 cases; no unmatched selectors | merge06-combined-canonical-mcp-restore.md |
| `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[request-guidance],[view-identity],[view-info],[presented-frame],[viewer-presentation],[translation-preview],[translation-commit]'` | exit 0, 10764 assertions / 58 cases | merge06-cumulative.md |
| `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` | exit 0, 68/68 pass, 72.79 seconds; no skips | merge06-ctest.md |
| `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[integration]'` | exit 0, 127714 assertions / 364 cases | merge06-integration.md |
| `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --log-file="$PWD/.scratch/viewport-integration/logs/merge06-valgrind.md" _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[translation-commit],[translation-preview],[node-edit],[node-position]'` | exit 0, 10086 assertions / 43 cases; 0 errors / 0 contexts, 0 definite/indirect loss, no suppressions | merge06-valgrind-tests.md; merge06-valgrind.md |
| `XDG_RUNTIME_DIR="$PWD/.scratch/viewport-integration/tmp/merge06-runtime" _out/_cmake/RelWithDebInfo/vsrTutorialIntegrationHeadless --exit-after-activation` | exit 0, isolated fixture; runtime directory created mode 0700 | merge06-headless.md |

An initial affected union included a nonexistent `[restore]` tag: it exited 0 with 13750 assertions/50 cases but explicitly printed that selector's no-match warning (`merge06-selection-restore-mcp.md`). This was discovery error, not restoration coverage. The actual named restoration/canonical union above and full suites subsequently exercised coverage without unmatched selectors; no failing tests were weakened or retried until green.

Runtime/build provenance (`merge06-provenance.md`) positively records correct CMake home/install prefix, enabled BUILD_TESTING/apps/interactive/CUDA/network/Lua/SDL3/integration/MCP, unchanged `-O2 -g -DNDEBUG` and existing executable-only `-Wl,--compress-debug-sections=zlib`; ELF debug sections retain compressed flag C. No cold configuration or feature/optimization changes. VSR integration is statically linked into the newly built test executable; installation installs the configured ANARI VSR dynamic device, not an invented installed test executable/SDK. Build-tree and installed device hashes match. Own `_out/lib` is first; inherited backend/system libraries remain available. Overlay remains about 4.6 GiB free, no deletion or ENOSPC.

## Scoped source/behavior inspection

Inspected retained immutable value-only PreparedTranslation consumption: routing `scene_id` is included in canonical request identity and checked against owner/launch/Scene plan, not authority. Commit copies retained exact matrix/SRT into ephemeral PreparedBatch, never calls preparation/math to rebase the approval. Every selected entry including inherited children is checked for node/structural/ancestry state and selected-ancestor normalization. Allocation/receipt reservation uses the shared actual atomic EditService path; final gate rechecks authority, expiry/deadline and dependencies before any write. Definite stale plans retire; bounded-work exhaustion is not misreported as permanent staleness. Current READ/EDIT and node-edit exposure are revalidated.

Inspected and independently executed tests cover relevant target/ancestor/inherited-child change-back, hierarchy changes, foreign/session/source expiry, exposure/source reattachment and withdrawal, no-write allocation/restoration/deadline denials, valid unrelated edits and Forest relocation. Literal affine expected values plus public approved matrices establish exact consumption, preservation of local linear state/cache/defaults and once-only nested movement. Common-parent remains unselected; 64 independent roots commit atomically.

Successful consumption denies other request reuse; same canonical admitted request replays historical receipts after restoration without another mutation. Canonical mismatch conflicts, malformed MCP input does not consume identity, current read withdrawal suppresses historical protected results. Actual loopback HTTP/MCP fixture executes fresh inspect → preview → commit → reinspect → conditional restore. Existing normal Batch Reference/activity/versions/restoration reservations are preserved. Cancellation before commit writes nothing; cancellation after commit is truthful, not rollback. The narrow Operations condition permits already committed synchronization/cleanup after withdrawal; tests cover owner/session retirement and held result-copy lifetime. Conditional one-way restoration recovers exact original matrices/cache under existing permanent target/required-ancestor history conflicts.

## Limits, readiness and outer gate

Ticket06 is accepted and landed, conflicts false, ready for outer integrated review; no merge blockers found. Worker verification/TDD red logs and requirement mapping were inspected as author evidence, separate from independent runs above.

Memcheck still reports 2392 possibly-lost bytes/15 blocks and 4186665 reachable bytes/180 blocks, with driver ioctl/address-range warnings. This targeted headless test is not universal leak-free/GPU/GUI instrumentation. No new actual viewer parity, arbitrary GPU/storage qualification, user live Scene edits or monitor-parity claim is made. Prior accepted ticket04 actual supported-frame qualification remains a prerequisite with its unit-SDL-scale HOST/MANAGED profile restrictions and disclosed GUI/Compute-Sanitizer/Massif limitations; unqualified GPU-only/unknown/non-unit-scale paths remain unavailable. This merger reran cumulative public regressions, not the complete production GPU qualification matrix.

Final independent whole-spec two-axis Standards/Spec review, evidence reconciliation and any resulting fixes remain the outer orchestrator's separate gate. Landing ticket06 does not claim whole-spec completion. Outer orchestrator owns progress ledger and workspace cleanup; neither was touched.
