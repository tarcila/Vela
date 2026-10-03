# Ticket15 bounded Dataset lifetime repair proof

Date: 2026-10-04. Workspace: `/home/tarcila/Code/ANARI/vela/broad-editing`.
Dispatch change: `wrzpwqww`, parent `olkkzuvl` / `565446db`.
Scope: one existing lifecycle Scenario in `src/tests/test_SciVisStudio.cpp` and existing-contract comments in `src/apps/interactive/scivisStudio/ProjectContext.h`. No implementation, schema, ownership-container or feature03 change.

## Causal finding and contract

1. `Project.h` owns `std::vector<Dataset>` by value. Its sorted-view API explicitly says collection pointers die with the next collection change. `project::findDataset` (`Project.cpp`) returns an address inside that vector, not a stable handle. `DatasetID` is the stable project-local identity in Studio CONTEXT; Dataset Reimport is transactional and preserves identity/shot associations. ADR0004 and ADR0006 preserve authoritative assets and transactional persistence, not record addresses.
2. `ProjectContext::addStaticDataset` appends a record and returns borrowed vector storage. Successful `saveProject` installs `m_project = std::move(save.project)` after the asset transaction; `openStagedProject` likewise replaces the Project. No inspected API promises persistent Dataset addresses. DatasetEditor borrows the current collection per UI build and captures Dataset IDs for deferred archive save/reimport/load operations. The repair does not invent or break a pointer-stability guarantee.
3. The original lifecycle test saved a copied ID but retained its original Dataset pointer across save. `resolveDatasetRoot` reads that record's ID and writes its root reference. Fresh baseline Memcheck traces invalid reads/writes to storage freed by Project move-assignment in save, originally allocated by addStaticDataset. Baseline assertions fail normally and happen to pass under Memcheck; allocation-dependent green assertions are not memory safety.
4. A single-variable probe reacquired with `project::findDataset(projectContext.project(), originalId)` before the first post-save root resolution. All original semantics then pass (35 assertions including the added non-null check), with zero Memcheck errors. No production implementation changed. This confirms the borrowed-pointer hypothesis; the live failed-reimport root is preserved when accessed through the current record. No prior-revision binary was rebuilt, and no exact regression-introduction attribution is claimed.

## Public regression and source review packet

The same original lifecycle Scenario remains; none of its root-preservation, archive identity, rename/removal/discovery or asset-file assertions were weakened. It now captures the live root before saving, reacquires after each of three saves, and checks ID/name/clean state/root/shot binding. Actual Archive Load and Dataset Removal exercise inventory mutations followed by reacquisition of the original record. Failed reimport after source deletion preserves its root, ID/name/persisted name/provenance/status/residency/clean state and sole enabled shot binding. Successful reimport retains ID and makes the dataset dirty; rename/save retains the replacement's live root and clears dirty state. No freed address is compared or dereferenced and no allocator-capacity assumption is used.

Header comments clarify returned pointers borrow `project().datasets`, recommend copied DatasetID plus lookup across inventory/Project replacement, and document successful save's existing record replacement versus live-subtree preservation. `ProjectContext.cpp`, Studio CONTEXT and all unrelated source are unchanged. Author inspection is not independent review. Outer orchestrator should review `jj diff -r wrzpwqww -- src/tests/test_SciVisStudio.cpp src/apps/interactive/scivisStudio/ProjectContext.h`; frozen source diff is also `logs/15-source-diff.txt`. The established review route remains authoritative; no duplicate route hole was filed. Ticket03 and its cumulative disposition were not edited.

## Exact fresh verification

Every shell invocation began with `cd /home/tarcila/Code/ANARI/vela/broad-editing &&`. Commands below omit only redirection to ignored `logs/15-*.txt` and tiny stdout summaries. Existing `_out/_cmake` / `_out` reused; cache still has Release, CUDA/UI/apps/interactive apps/network/Lua/SDL3 enabled. Build/install ran before baseline, probe and final runtime checks; no configure or disabled feature.

| Command | Actual result / logs |
| --- | --- |
| `cmake --build _out/_cmake -j 6 && cmake --install _out/_cmake` (baseline) | exit0; `15-baseline-build.txt`, `15-baseline-install.txt` |
| `_out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'` (baseline) | exit1, **1 case/10 assertions:9 passed,1 failed**; `15-baseline-isolated.txt` |
| `valgrind --tool=memcheck --error-exitcode=99 --leak-check=no _out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'` (baseline) | exit99, **36 errors/20 contexts**, no suppressions, despite34/34 assertions in1 case; `15-baseline-valgrind.txt` |
| Same build/install and same two isolated commands after lookup-only probe | exits0, **1 case/35 passed assertions** normally and under Memcheck; **0 errors/contexts**; `15-probe-{build,install,isolated,valgrind}.txt` |
| `clang-format -i src/tests/test_SciVisStudio.cpp src/apps/interactive/scivisStudio/ProjectContext.h` then `clang-format --dry-run --Werror src/tests/test_SciVisStudio.cpp src/apps/interactive/scivisStudio/ProjectContext.h` | exit0; final dry-run no diagnostics, `15-final-format.txt` |
| `cmake --build _out/_cmake -j 6 && cmake --install _out/_cmake` (final) | exit0; `15-final-build.txt`, `15-final-install.txt` |
| `sha256sum _out/lib/libanari_library_vsr.so _out/_cmake/libanari_library_vsr.so` | both `a47cc3beb17eeacdd479467328741622d2297b09bc362586bea9cd3df2b5f092`; changed test executable rebuilt and UI callers rebuilt for header change |
| `_out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'` five times | each exit0, **1 matched case/84 passed assertions**; `15-repeat-1.txt` through `15-repeat-5.txt` |
| `valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=possible --errors-for-leak-kinds=definite _out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'` | exit0, **1 case/84 assertions,0 errors/contexts,0 definite/indirect leaks**, no suppressions; `15-final-isolated-valgrind.txt` |
| `ctest --test-dir _out/_cmake --output-on-failure` twice | each exit0, **68/68 passed**,68.51s and68.78s; `15-final-ctest.txt`, `15-repeat-ctest.txt` |
| `_out/_cmake/vsrTests '[SciVisStudio]'` | exit0, **80 cases/1,575 passed assertions**; `15-final-studio.txt` |
| `_out/_cmake/vsrTests '[Scene],[Forest],[DataTree],[DataPath],[ObjectPool],[Object],[Array],[Parameter],[RenderIndex]'` | exit0, **52 cases/1,110 passed assertions**; `15-final-core.txt` |
| `_out/_cmake/vsrIntegrationTests '[transfer-validity],[membership-validity],[payload-validity],[payload-ownership]'` | exit0, **30 cases/8,894 passed assertions**; `15-final-feature.txt` |
| `_out/_cmake/vsrIntegrationTests '[integration]'` | exit0, **311 cases/117,175 passed assertions**; `15-final-integration.txt`; actual polling-dependent count, not inherited118,085 |
| `valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=possible --errors-for-leak-kinds=definite _out/_cmake/vsrIntegrationTests '[transfer-validity],[membership-validity],[payload-validity],[payload-ownership]'` | exit0, **30 cases/8,894 assertions,0 errors/contexts,0 definite/indirect leaks**, no suppressions; `15-final-feature-valgrind.txt` |

## Limits and disposition

Memory verification covers the only changed Dataset case (including all newly added operations) and the cumulative30-case feature matrix, not every Studio case or every GPU allocation. Both memory runs disclose unsupported driver ioctl warnings and possible/reachable initialization allocations: lifecycle2,392 possible bytes/15 blocks and4,186,608 reachable bytes/180 blocks; feature2,376 possible bytes/15 blocks and4,186,611 reachable bytes/180 blocks. No blanket leak-free, sanitizer, independent-review, GPU ANARI capture or GUI qualification claim.

Ticket15 repair acceptance is satisfied with fresh full-suite evidence, not a suite exclusion. Full gates are now actually green in this dispatch, but ticket03's closure and cumulative feature acceptance remain the outer orchestrator's responsibility, including independent review of this bounded repair. Frozen spec/addenda, sibling tickets, ledger/run records, executor/configuration, historical camera/capture/user artifacts and other workspaces were not mutated. No holes filed; no publication/subdelegation. Base frontmatter declares4500 seconds, depth1 observed, no actual runtime deadline field exposed; finished well before55-minute stop assessment.
