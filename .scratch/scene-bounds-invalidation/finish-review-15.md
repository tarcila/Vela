# Independent Dataset correction and restored gate evidence

Fixed point:trunk `e04c2853d04d3076b4f0870efc49f787bd870010`.
Frozen target:`ee8501fbf42c7a1eb4ea2eee58b12b49698d9271`.
Current15 correction:`wrzpwqww` / `c6326c5c`.
Full diff:`jj diff --from e04c2853d04d3076b4f0870efc49f787bd870010 --to ee8501fbf42c7a1eb4ea2eee58b12b49698d9271`.
Commits:`jj log -r 'e04c2853d04d3076b4f0870efc49f787bd870010..ee8501fbf42c7a1eb4ea2eee58b12b49698d9271' --no-graph`.

## Standards

APPROVE the scoped correction; no required changes. Only the existing lifecycle Scenario and ProjectContext header borrow-lifetime comments change. The Scenario now reacquires records by copied Dataset ID at all relevant save/reimport/inventory mutation boundaries and strengthens root/metadata/shot/asset assertions. Header comments document existing vector-record invalidation, not new pointer stability. No production implementation, container or schema changed.

Repeated lookup/REQUIRE pairs are an optional local readability suggestion (18); the pattern follows existing suite convention and explicit boundaries are useful. No broad test-infrastructure refactor is justified by that suggestion.

## Spec and correctness

APPROVE15's repair, no required changes. Project owns vector Dataset records, findDataset returns their borrowed addresses and successful save replaces those records. Domain identity is Dataset ID; existing deferred UI callers use IDs. The original test's retained pointer was invalid, not a promised production stable handle. Tests keep the original asset/root-preservation requirements and strengthen failed-reimport transactional checks. No previously failing assertion was dropped or loosened.

Independent fresh reviewer reproduced using current built binaries (no new compile/reconfigure):
- lifecycle Scenario:1case/84assertions, exit0;
- isolated full Memcheck:exit0,0errors/0contexts, versus prior36errors/20contexts;
- combined feature matrix:30cases/8894assertions, exit0;
- full CTest:68/68passed, exit0,68.61s.

Memcheck's NVIDIA initialization/TLS possible/reachable allocations remain disclosed (2392possibly-lost bytes/15blocks and4186608reachable bytes/180blocks). No blanket leak-free, full-Studio memory-scan or GPU/capture evidence claim. Author separately rebuilt/installed, repeated the isolated case five times, ran full CTest twice and311integration/80Studio/52supporting cases, and reran combined feature Memcheck0errors. Exact commands/current counts are in15-verification.md; no exact prior-binary introduction claim.

## Cumulative evidence-map check

A separate fresh reviewer inspected the previously uncovered60-story map. All60 numbered rows appear exactly once, all cited primary/supporting/unchanged scenario names exist, and sampled assertion bodies substantiate sampled obligations. No invented test references or required corrections were found.

Limit:only approximately8rows had assertion bodies independently read in full in that map-only audit; the remaining rows were structurally checked against symbols, not exhaustively assertion-by-assertion. That audit itself reran no binaries and checked original03logs (including then-failing67/68), not the new15gate evidence. Current runtime gate is supplied by the independent15rechecks above. Earlier scoped01/02/03reviews provide their separately disclosed source coverage. Do not conflate these stages into an exhaustive deep60-row or older-trunk audit.

## Disposition

15's Dataset lifetime/full-suite blocker is repaired and independently approved.03's current full-suite/memory/review evidence is restored without reimplementing reviewed transfer source.03 remainsready-for-human for final cumulative acceptance/closure;15isresolved. No parent authorization to silently close03 follows from report verdicts, and no additional source dispatch/post-finish loop occurs.

18 is new optionalneeds-triage; earlier triage records stay open. The immutable scene-bounds spec, historical camera/capture tracker, encoder/image limits and unrelated workspaces remain untouched. No complete geometric bounds computation, capture or future8K implementation is claimed.

Stop:frontier-empty.
Iterations used:1.
Resolved15 source:wrzpwqww.
Preserved03 source: vxxspsoo; final closure pending.
Holes:none new; prior15 blocker repaired.
