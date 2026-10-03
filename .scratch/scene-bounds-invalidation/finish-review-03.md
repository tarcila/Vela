# Independent subtree/cumulative finish review — run3

Fixed point:trunk `e04c2853d04d3076b4f0870efc49f787bd870010`.
Frozen target:`e4b9f656500cf6421ea8b430c39a0f9fa778bae7`.
Current03 WIP:`vxxspsoo` / `08f72000`.
Full diff:`jj diff --from e04c2853d04d3076b4f0870efc49f787bd870010 --to e4b9f656500cf6421ea8b430c39a0f9fa778bae7`.
Commit list:`jj log -r 'e04c2853d04d3076b4f0870efc49f787bd870010..e4b9f656500cf6421ea8b430c39a0f9fa778bae7' --no-graph`.

Two fresh independent Standards and Spec/correctness reviewers ran source-read-only. No implementation dispatch, scope authorization, source correction or gate waiver follows from their reports.

## Standards

APPROVE current six-source/test-file03 diff, no critical/required changes. Optional/default-neutral CopyValidation preflight and effective-reparent membership tracking respect generic-core independence, existing guards, code layout and error conventions. No duplicate membership/coarse callback was added for copying, which already delegates insertion to the instrumented path.

Optional documentation of deliberately supported foreign-Forest source copying is16. Ownership-comparison helper suggestion is the existing09 concern, annotated rather than duplicated. Generic source-container compatibility must not be confused with stricter Scene-owned copy preflight or cross-Forest reparent restrictions.

## Spec and correctness

APPROVE the scoped transfer source; completion/resolution explicitly withheld because the cumulative full-suite gate fails. Early root/self/cross-Forest/ancestor/no-op reparent guards precede successful membership tracking. Scene copy preflight walks source content before insertion and rejects foreign Scene ownership atomically. Receiving Layer attachment, clone identities, source-destruction behavior, precise histories/counts and already accepted01/02 guards are exercised by actual public tests.

Independent reviewer reproduced:
- combined payload/ownership/membership/transfer matrix:30cases/8894assertions passing;
- full CTest twice:67/68passing, same Studio root-resolution failure;
- isolated Studio Memcheck:36errors/20contexts with assertions passing under a different allocation layout.

No fresh build/install by reviewer; author separately recorded enabled full build/install,9transfer cases/2025assertions,311integration cases/118085assertions,52supporting/1110, formatting and combinedMemcheck0errors/0definite-or-indirect leaks. GPU/TLS possible/reachable allocations and driver limits remain disclosed. Full CTest is **not** green and isolated Studio assertions under Memcheck do not override its memory errors.

One of five Clang analyzer diagnostics involving EditResult::outcome was source-classified by reviewer as a false positive due to its default member initializer;17 records narrow report clarification. Do not infer the other warnings are dismissed or analyzer output is clean.

## Dataset blocker15

Independent review confirms stale Dataset storage after `ProjectContext::saveProject` assigns the saved Project and later code uses a retained prior Dataset pointer in `resolveDatasetRoot`. The relevant Studio Dataset/test/save code was not changed by this scoped source diff; the full-range review likewise finds no relevant changes to that pointer sequence. No old-revision runtime rebuild was performed, so no assertion that all earlier binaries failed or a precise regression-introduction boundary is made. Heap/layout-dependent assertion success does not make the stale-pointer trace safe.

15 needs narrowly authorized investigation/repair of the test or promised Dataset pointer contract, followed by fresh enabled build/install, isolated memory checks and full CTest. Reacquiring by stable ID is a candidate only if that is the actual existing API contract; pointer-stability promises must be checked instead of guessed. The orchestrator did not change Studio code or waive the failing requirement.

## Coverage and disposition

Deep independent review covered current03's Forest/Layer transfer code and three changed test files, plus reviewer examination/reproduction of15. It did not exhaustively re-audit all older accumulated trunk code or independently rederive every row of the author's60-story map. This is scoped source approval and blocker evidence, not full-branch/full-spec qualification.

16/17 are new optional needs-triage findings;09 has the same optional concern annotated. Existing04/06–14 otherwise unchanged.03 remainsready-for-human, blocked15; source and positive matrix preserved. No source correction, automatic post-finish loop, downstream complete-bounds/capture handoff or image-limit change is authorized.

Stop:frontier-empty.
Iterations used:1.
WIP:vxxspsoo (03).
Hole:15 (contradicts), full-CTest Dataset lifetime/scope blocker.
