# Independent finish review — scene-bounds-invalidation run

Fixed point: trunk `e04c2853d04d3076b4f0870efc49f787bd870010`.
Frozen target: `02ce3e4c811875eb3b579039a7d18c5629db887f`.
Current source WIP: `xrwvlnwz` / `2d02ef5b`.
Full diff command: `jj diff --from e04c2853d04d3076b4f0870efc49f787bd870010 --to 02ce3e4c811875eb3b579039a7d18c5629db887f`.
Commit list command: `jj log -r 'e04c2853d04d3076b4f0870efc49f787bd870010..02ce3e4c811875eb3b579039a7d18c5629db887f' --no-graph`.

Two fresh code-reviewer children ran independently for Standards and Spec/correctness, selected through the normal user agent scope. No new implementation dispatch occurred. No source/ticket/config modifications were performed by reviewers. Review findings are data, not authorization to weaken requirements, change standards or resolve WIP automatically.

## Standards

Reviewer requested changes to document revocable lifetime-control ownership under CODING_STANDARDS sections6/16 and to explain lifetime-state friendship. The first maps to06: owning shared control blocks differ from weak non-owning observations; section16's literal raw-pointer requirement needs a narrow justified disposition, not an unsafe replacement of weak guards. Friendship rationale maps to07: section13 has a commented friend example but no blanket mandatory-comment rule, so this is an encapsulation/readability judgement, not an established hard violation.

Optional report concision and weak-owner comparison clarity map to08/09. Preserve evidence, expiry semantics and separate meanings if those suggestions are accepted. The optional/default-neutral Forest storage/binding hooks were found justified by actual ownership/relocation requirements, not speculative generality. Checked epoch overflow matches established lifetime-clock behavior. Known adjacent activation overload04 was recognized as pre-existing, not duplicated.

## Spec and correctness

Deep review of01's source and tests found **no critical/required-blocking functional issue in that verified scope**. Reviewer matched the payload/owner criteria against Scene, Layer, LayerNodeData, revocable state, Forest and Identity behavior;02/03 membership/transfer tracking is explicitly not implemented, not implicitly qualified.

Reviewer independently executed existing built binaries:
- focused payload validity/ownership twice:10cases/1201assertions each, all passed;
- full integration four times:291passing cases each, assertion totals109594–109825 (poll-dependent variation, not a demonstrated failing case or01 regression);
- CTest:68/68passed.

No fresh build or memory-tool rerun by that reviewer. Author's full enabled build/install,10focused/291integration/50supporting cases and Valgrind/analyzer evidence remain separately documented in01-verification.md. Valgrind reported0errors and0definite/indirect leaks while disclosing GPU initialization/TLS possible/reachable allocations and ioctl limitations; no blanket leak-free/GPU capture claim.

Optional assertion-total interpretation and underspecified counter-guard discipline concerns map to10/11. noteChange itself checks a live Scene pointer and terminates on wrap; no concrete exploitable or failed guard path was provided.

## Coverage limit

The frozen trunk range is nonempty and contains86product source/doc files with roughly36Kchanged lines plus historical scratch artifacts (303files/~101Klines in the complete diff). Both reviewers deeply inspected current01's ownership/revision source and relevant tests. Standards scanned older integration/MCP/UI/CUDA portions at structure/stat level; Spec did not deeply verify the other~75product files. **This is not exhaustive fresh approval of the entire accumulated trunk diff.** No narrower scope was silently represented as full coverage. It does provide the required independent review channel and focused01 findings/evidence, with the Standards disposition still pending.

## Disposition

Filed all actionable/optional concerns06–11 asneeds-triage, distinguishing mandatory-rule mismatch, judgement calls and absent defect evidence.04 remains the pre-existing adjacent needs-triage finding. Independent review is now obtained externally to the no-subdelegation writer, but01 remainsready-for-human/WIP and05 remains its review-disposition gate until the human determines any required corrections and closure. No parent source fixes, standards edits, gate waiver or automatic implementation loop follows finish review.

Work-tickets stop:frontier-empty.
Implementation iterations used:1.
WIP:xrwvlnwz (01).
02and03 remain blocked; complete scene-bounds validity, boxes/capture and image-policy changes are not claimed.
