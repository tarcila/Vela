# Camera/capture serial run — 2026-10-03

Spec and addenda remained immutable. Local graph published; no remote publication or unrelated workspace recovery. Original pi-integration user configuration and screenshot were not touched.

## Dispatch accounting

Authorized budget: 10, serial. Actual dispatches: **11**. Orchestrator mistakenly redispatched already-blocked 28 once after dispatch 10. This was an orchestration error, not an implementer failure or authorized budget extension. Extra dispatch committed only a second blocked-evidence comment; no source changes. No further implementation dispatch follows this report.

| Dispatch | Ticket | Outcome | Change |
|---|---|---|---|
| 1 | 01 | split 13,14 | rxlnqstqvvzu |
| 2 | 02 | split 15–18 | pkmlulkymkxo |
| 3 | 13 | split 19,20 | qywqyrlvztrl |
| 4 | 15 | split 21–23 | nxvuorrpmzrp |
| 5 | 19 | resolved | sstvyutrpnqs |
| 6 | 20 | ready-for-human; hole24 | sskzwxtynpsl |
| 7 | 21 | split 25,26 | kpxzysumqykm |
| 8 | 25 | split 27,28 | wlssrnwrrono |
| 9 | 27 | resolved | uxruqyutrstl |
| 10 | 28 | ready-for-human; hole29 | qwnpvlmoytxk |
| 11 (error) | 28 | ready-for-human, unchanged blocker | kurusmrnrvvs |

Split-parent dependencies were reconciled to completion of replacement chains. Splits are not implementation completion. No current full host bounds or capture is claimed.

## Standards

Fresh independent code-reviewer reported three readability/consistency concerns: full Job scratch guard (30), sibling query schema convention (31), duplicated fixed limits (32). All filed as **needs-triage**, explicitly judgement calls rather than established correctness defects. No automatic fixes or next implementation loop.

## Spec

Separate fresh code-reviewer approved completed 19 and 27 within their sliced claims. Camera/capture as a whole remains incomplete: effective binding observation is read-only with capture closed; scene bounds only reports truthful missing_binding. Actual host values, analytic/triangle coverage, viewer observation, capture lifecycle/quality, CPU/CUDA ANARI qualification and local PNG saving remain future work.

Reviewer reproduced `[bounds],[bounds-mcp],[view-info]`: **13 cases, 680 assertions**; `[integration]`: **276 cases, 36,453 assertions**. Worker27 also ran six matching CTest suites, all passed, after full enabled build/install. No current GPU capture qualification.

## Review coverage limit

Fixed point pinned to trunk `e04c2853d04d3076b4f0870efc49f787bd870010`; target `de2024eccc7e`. Full diff is nonempty (271 files including historical scratch artifacts). Reviewers concentrated on current view-info/bounds capabilities. Spec reviewer explicitly did not re-review prior integration/broad-editing source/addenda in depth or independently rerun blocker probes. Thus this is **not a fresh exhaustive approval of the entire accumulated trunk diff**. Prior reviews do not erase this limitation.

## Human blockers

24 (contradicts): current full-HD fixed-Huffman encoder output actually exceeds the 8MiB PNG cap, conflicting with whole-envelope conservative-bound validation. Decide independent publication cap versus scoped encoder/default-envelope change; do not silently alter defaults.

29 (extends): six public Scene/node/hierarchy mutation classes leave scene/inspection tokens unchanged. Authorize separately scoped scene/core invalidation repairs before publishing complete current host boxes. No weakened cache contract or unbounded scene fingerprint adopted.

20 and 28 remain ready-for-human, respectively blocked by24 and29. No WIP revisions from this run; no further frontier ticket is runnable before human decisions. See ticket records for durable reproductions and exact verification.
