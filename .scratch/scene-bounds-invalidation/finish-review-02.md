# Independent membership finish review — run2

Fixed point:trunk `e04c2853d04d3076b4f0870efc49f787bd870010`.
Frozen target:`1fc970ba7136b32a26aa61e4425f5a319d36cce8`.
Current02 source WIP:`vuxprvxk` / `3b1010d3`.
Full diff:`jj diff --from e04c2853d04d3076b4f0870efc49f787bd870010 --to 1fc970ba7136b32a26aa61e4425f5a319d36cce8`.
Commits:`jj log -r 'e04c2853d04d3076b4f0870efc49f787bd870010..1fc970ba7136b32a26aa61e4425f5a319d36cce8' --no-graph`.

Two fresh independent code-reviewer children, separate Standards and Spec/correctness axes, reviewed source read-only. No implementation iteration, source correction, configuration change or automatic gate closure occurred during review.

## Standards

APPROVE for the current nine-file02 source/test diff; no critical or required changes. Optional/default-neutral Forest membership instrumentation, Layer revocation and dependency gates meet current needs without moving Scene semantics into core. Existing generic Forest/Data Tree behavior is preserved. Reviewed all11new membership scenarios and generic instrumentation tests.

Two optional judgement calls: near-duplicate membership/payload dependency guards (12), repeated explicit signal/removal-revocation sequence (13). Guards are similar but not identical: payload tracking has extra live-owner/attachment checks that must not be lost by extraction. The removal repetition largely predates02. New Scene friendship includes a why-comment; old triage06–11 is not automatically resolved or duplicated.

## Spec and correctness

APPROVE for in-scope02 WIP; no required source changes. Real successful insertions/erase/subtree-clear invalidate dependencies; allocation/root initialization/reserve/gaps/failed slots/direct empty no-ops do not. Removal revokes dependency attachment separately from live object resolution, counts and lifetime safety. Both getter orders, Scene locality, explicit callbacks, mixed batches, identities, surviving precise histories and accepted01 ownership/payload behavior are exercised.

Independent runtime rechecks on existing built binaries (no fresh compile or install by reviewer):
- membership:11cases/5668assertions;
- combined membership/payload/ownership:21cases/6869assertions;
- supporting core filter:51cases/1087assertions;
- CTest:68/68passed;
- installed/build device SHA256 matches `bcabab106e3e3553c127c96389d0d0e95598049b1609bf5337e9e18becdc2950`.

Author separately recorded full enabled build/install,302integration cases/115199assertions, Valgrind21focused cases0errors/0definite-or-indirect leaks and disclosed GPU possible/reachable allocations/ioctl limits, plus analyzer/formatting. Review did not independently rerun Valgrind or claim fresh build verification from merely executing existing binaries.

Optional abrupt-counter-exhaustion host-policy note is14; fail-safe termination already satisfies the spec and established clock behavior, not a defect or new blocking decision.

## Scope and report interpretation

The fixed trunk range remains nonempty, but deep review concentrated on the nine changed02 files and required01 seams; older accumulated product code was not exhaustively re-reviewed. This is not fresh whole-branch approval. Source membership work is approved within02's contract, not completed whole-scene bounds validity, boxes or capture.

Some reviewer overview wording overstates transfer absence: copy_subtree has no direct new tracking call but delegates insertion to an instrumented helper, so copying can already emit membership changes.03 still owns explicit copy/reparent/destination-transfer behavior and cumulative qualification. Likewise object-use-count release is separate from noteDependencyChange; the latter gates validity, not an unconditional decrement. These overview descriptions do not create a new defect or justify skipping03.

## Disposition

New optional findings12–14 filedneeds-triage; existing04/06–11 unchanged.02 remainsready-for-human with its last gate unchecked until proper review disposition/closure; do not relabel the author as independent or infer gate authority from an APPROVE verdict alone.03 remainsblocked02. No new holes or splits; no automatic post-finish implementation loop.

Stop:frontier-empty.
Iterations used:1.
WIP:vuxprvxk (02).
