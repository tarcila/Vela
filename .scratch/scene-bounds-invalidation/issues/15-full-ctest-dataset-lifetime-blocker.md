# 15: Resolve full-CTest Dataset lifetime blocker

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Kind:** contradicts
**Files:** src/tests/test_SciVisStudio.cpp, src/apps/interactive/scivisStudio/ProjectContext.h, src/apps/interactive/scivisStudio/ProjectContext.cpp, src/apps/interactive/scivisStudio/CONTEXT.md
**Specification:** ../spec.md, Testing Decisions 8–10; ticket03 full-CTest/cumulative gate

## Load-bearing conflict

Ticket03 requires applicable full CTest to pass and preserves memory safety, while this dispatch explicitly excludes unrelated Studio source/test repairs. The fresh enabled full build exposes a repeatable failure in the unchanged `SciVis Studio dataset lifecycle workflows preserve asset semantics` case at `src/tests/test_SciVisStudio.cpp:2648`. Both full runs pass67/68; the isolated case fails1/10 assertions. Qualification cannot proceed by excluding this suite, changing allocator/layout to conceal undefined behavior, loosening the assertion, or declaring the full gate green.

This record stops03 for an authorization/compatibility decision, not a helper split or short-budget objection. No Studio code or historical tickets were changed. Independent finish review remains on the existing outer-orchestrator route; no duplicate review-route hole is filed.

## Exact evidence

Every command ran from `/home/tarcila/Code/ANARI/vela/broad-editing` with the required `cd` prefix. Logs are ignored under `../logs/`.

- `cmake --build _out/_cmake -j 6 && cmake --install _out/_cmake`: exit0, unchanged CUDA/UI/network/Lua/SDL3-enabled configuration; freshly installed device hash matches build device.
- `ctest --test-dir _out/_cmake --output-on-failure`: exit8 twice, **67/68 passed**,68.64s and68.48s; `03-final-ctest.txt`, `03-repeat-ctest.txt`. SciVis Studio:80 cases/1,501 assertions, one failed assertion (`resolveDatasetRoot(*dataset) == originalRoot`, expansion0==1).
- `_out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'`: exit1, **1 matched case/10 assertions,9 passed/1 failed**; `03-studio-isolated.txt`.
- `valgrind --tool=memcheck --error-exitcode=99 --leak-check=no _out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'`: exit99, **36 errors/20 contexts**, no suppressions; `03-studio-valgrind.txt`. Assertions happen to pass34/34 in1 case under this different allocation layout; that does not make the run memory-safe or qualification green.
- Memcheck identifies invalid reads/writes to Dataset storage allocated by `ProjectContext::addStaticDataset`, freed by `Project::operator=(Project&&)` in `ProjectContext::saveProject`, then accessed through `ProjectContext::resolveDatasetRoot`. The test retains the original Dataset pointer across `saveProject(root)`. No parent-revision baseline rebuild was performed, so exact change-attribution beyond this observed stale-pointer trace is not asserted.
- Separately, the scoped public payload/membership/transfer matrix passes **30 cases/8,894 assertions** both normally and under Memcheck, exit0 with0 errors and0 definite/indirect leaks; full integration311 cases and supporting52 core cases pass. See [03-verification.md](../03-verification.md).

## Required human action

Authorize a narrowly bounded investigation/repair of this Dataset lifetime test/contract outside03's current source scope, or supply a justified disposition with equivalent fresh full-suite and memory-safety evidence. Do not waive the failing qualification requirement or hide the undefined behavior. If pointer invalidation across save is established behavior, the test should reacquire the Dataset by stable ID at the proper boundary; if callers are promised pointer stability, the implementation requires investigation instead. This executor has not selected or implemented either repair.

After authorized repair: rebuild/install the enabled tree, rerun the isolated case under Memcheck and full CTest, preserve03's positive public matrix, and obtain independent correctness/Standards finish review through the established route before resolving03. Complete host-bounds/capture work remains unqualified. No spec/addendum edit, configuration change, other-workspace mutation or remote publication is authorized by this record.

## Comments

### 2026-10-04 independent finish evidence
Outer Spec reviewer independently reproduced67/68CTest twice and the isolated Dataset Memcheck36errors/20contexts, while30combined feature cases/8894assertions pass. Source inspection confirms Project replacement invalidates the retained Dataset storage and that the relevant Studio pointer sequence was unchanged by this scoped work. No parent-revision runtime rebuild was done, so exact binary regression attribution is still not asserted. This is confirmed memory evidence, not an allocator-specific assertion to suppress.
Narrow test-versus-API-contract investigation and any repair still need separate scope authorization.03source review is otherwise approved, but source approval does not satisfy the failing cumulative full-suite gate or close15.

### Human authorization — implementation ready
User explicitly authorized the bounded Dataset-lifetime investigation and repair. This permits the failing lifecycle test and, only if the existing contract requires it, the narrowly relevant ProjectContext implementation/documentation. It does not authorize a broad Studio ownership redesign, archive schema change, weakened assertions, omitted suites or changes to03/source limits. Preserve the independently reviewed03 WIP and its positive30-case matrix.
The outer orchestrator reproduced the isolated baseline again:1case/10assertions,9passed/1failed on retained root equality. Ranked falsifiable hypotheses:cached borrowed pointer across Project replacement (ID reacquisition should eliminate invalid access); documented production pointer-stability promise (if present, test-only reacquisition is insufficient); failed reimport changes actual live root (if so, ID reacquisition still fails). Verify contract/callers rather than assume. This is an existing red-capable loop, not a new product interview.

## Authorized acceptance criteria

- [x] Confirm Dataset ID versus borrowed-record lifetime from existing domain/API/caller behavior; determine test misuse versus production contract defect. State the causal finding and avoid unsupported regression-introduction attribution.
- [x] Implement the smallest supported correction while keeping the root-preservation/asset semantics assertions. Reacquire by stable Dataset ID at appropriate Project mutation boundaries only if records are borrowed; repair production only if pointer stability is genuinely promised. A concise contract clarification may document existing behavior, not invent a new incompatible promise.
- [x] Preserve the original failing scenario and add/tighten public ProjectContext regression evidence covering repeated save/mutation boundaries, stable ID/root identity and failed-reimport transactional preservation as needed. Use actual operations, not private allocator assumptions or access to freed records.
- [x] Fresh enabled build/install; isolated lifecycle test passes repeatedly and isolated Memcheck returns0 with no invalid accesses (previously36errors/20contexts). Check all touched Dataset regression cases under memory verification; do not call assertions-green memory-safe if Memcheck fails.
- [x] Full applicable CTest passes68/68, with the full integration and30combined feature cases still passing. Run focused Studio/core checks and formatting, record exact commands/positivecounts/memory limits. No coldconfigure/CUDA disable, skipped suites, allocator concealment or loosened assertions.
- [x] Provide bounded source/proof record for outer independent review; preserve03 cumulative closure for its actual full evidence and gate disposition. No author analysis relabeled independent, no duplicate review-route hole or unrelated triage/historical/configuration edits.

### 2026-10-04 iteration — resolved

Confirmed test misuse of borrowed Dataset storage, not a production pointer-stability defect: Project owns vector records, findDataset returns their addresses, sorted views already document invalidation, save replaces the Project, and deferred UI callers capture IDs. Fresh baseline reproduces1case/10assertions (9pass/1fail) and Memcheck36errors/20contexts. Lookup-only probe makes the same root-preservation semantics pass35/35 with0memory errors. No prior-revision binary was rebuilt; exact introduction attribution is not asserted.
Corrected the existing public lifecycle Scenario to reacquire by copied DatasetID. Strengthened it across three saves, archive-load/removal inventory mutations, failed-reimport metadata/root/shot preservation and successful reimport/rename/save. Original asset/root assertions remain intact; the sole touched regression passes84assertions. Header comments clarify existing borrowed lifetime; no ProjectContext implementation, container, schema or broad Studio repair was needed.
Bounded causal/contract evidence, exact commands, source-review packet and limits are in [15-verification.md](../15-verification.md), detailed ignored logs `../logs/15-*.txt`. Ticket03 source and disposition are untouched. This repair resolves15's authorized blocker; independent review and03 cumulative closure remain with the established outer route, not author self-certification or an automatic complete-bounds/capture claim.

Verified: `cmake --build _out/_cmake -j 6 && cmake --install _out/_cmake` → exit0, existing CUDA/UI/network/Lua/SDL3-enabled tree, fresh install before runtime checks, installed/build device hashes match. `clang-format --dry-run --Werror src/tests/test_SciVisStudio.cpp src/apps/interactive/scivisStudio/ProjectContext.h` → exit0 after formatting.
Verified: `_out/_cmake/vsrTests 'Scenario: SciVis Studio dataset lifecycle workflows preserve asset semantics'` five runs → each exit0,1matched case/84passed assertions. `valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=possible --errors-for-leak-kinds=definite` on that same command → exit0,84/84assertions,0errors/contexts,0definite/indirect leaks, no suppressions.
Verified: `ctest --test-dir _out/_cmake --output-on-failure` twice → exit0,68/68passed,68.51s/68.78s. `_out/_cmake/vsrTests '[SciVisStudio]'` →80cases/1575assertions; core Scene/Forest/DataTree/DataPath/ObjectPool/Object/Array/Parameter/RenderIndex filters →52cases/1110assertions; `_out/_cmake/vsrIntegrationTests '[integration]'` →311cases/117175assertions, all exit0. Actual polling-dependent integration count is recorded, not inherited from03.
Verified: `_out/_cmake/vsrIntegrationTests '[transfer-validity],[membership-validity],[payload-validity],[payload-ownership]'` normally and under the same full Memcheck options → each exit0,30cases/8894assertions; Memcheck0errors/contexts and0definite/indirect leaks. CUDA/TLS possible/reachable allocations and unsupported driver ioctl remain disclosed in the proof, not blanket leak-free/GPU evidence. No full-Studio Memcheck or independent review is claimed.
Holes filed: none. Reserved18–23 unused. Base frontmatter declares4500seconds, exposed depth1, runtime deadline field unavailable; finished before55-minute stop assessment. No spec/addenda/sibling ticket/ledger/executor/config/historical/user-artifact/other-workspace edits, rawgit, push or subdelegation.
