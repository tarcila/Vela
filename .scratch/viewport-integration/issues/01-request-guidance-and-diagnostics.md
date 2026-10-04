# 01: Request knowledge and bounded diagnostics

**What to build:** Agents can construct valid minimal inspections and understand transform inheritance; invalid requests identify the rejected argument without weakening validation or leaking protected data.
**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Truthful Views, Presented Frames and Safe Selection Translation](../spec.md), stories59–74 and114–120; decisions13–15.

## Current behavior

Native inspection schemas require request_id/scene_id, but flat mixed-kind discoverability has encouraged over-filled object/node filter payloads. Native omission-based node query and wire-token edit/restore fixtures pass; a legal live Queen_B/pawn query through an omission-preserving client remains unverified. No standard Pi optional-forcing defect was established. Empty strings can be meaningful filters, and all filters intersect.

## Acceptance criteria

- [x] Tool descriptions/structured guidance carry concise object/layer/node minimal examples, omit-versus-empty semantics, fresh handle/version requirements, world-origin conventions and parent/child/sibling inheritance examples. No chess rules or guessed piece parent.
- [x] Already-invalid syntax/type/kind-inapplicable arguments return stable outcome codes plus bounded details(field,kind,constant reason), at most16entries and a finite byte cap. Echo no arbitrary large values, private Scene data, credentials or addresses. Live/foreign identity checks follow current authority and safe points.
- [x] Preserve accepted legacy requests, meaningful empty-name filters, optional omissions, intersections and strict kind-specific validation. No zero-handle shortcuts, blanket empty stripping, unproven nullable rewriting or incidental unknown-property compatibility changes.
- [x] Verify native schema, actual model/client exposure and transported arguments. Change a client adapter only if omission loss is demonstrated; additive narrower tools are permitted only with a justified compatibility/proof path, not as an assumed backend defect fix.
- [x] Public headless/MCP regressions cover minimal omissions, exact empty filters, malformed/mixed-kind/placeholder inputs, bounded diagnostics, pre-admission rejection/request-ID behavior and inspect→versioned edit→reinspect→restore. Preserve existing cases, not frozen historical assertion totals.
- [x] Demonstrate a legal minimal Queen_B/pawn lookup on an isolated real viewer/client path using fresh IDs without taking over the user's controller. No user's Scene edit, camera change or viewer restart is authorized by this ticket.
- [x] Build/install the existing enabled configuration and run focused protocol plus full applicable regressions, formatting and relevant memory checks. Record actual commands/counts and genuine remaining limitations in Comments.

## Out of scope

View/frame implementation, translation preview/commit, hidden fallback, user configuration/controller/Scene mutation, historical ticket closure and unrelated client changes. Preserve frozen inputs and original working-copy files.

## Comments

Approved six-ticket graph. Baseline gate is execution setup, not a product ticket. One fresh long-sitting executor context, not a20-minute helper split. Independent review is an integration-stage gate provided by the orchestrator after merging; author completion must not falsely claim it happened, but a restricted worker need not invent a duplicate missing-review-route hole. Code/test completion and final whole-spec review remain distinct.

### 2026-10-04 iteration — resolved
Added tool-visible minimal examples, omission/empty/intersection semantics and literal RootDelta/BodyTop once-only inheritance/sibling examples. Native inspection and MCP now share closed-field/reason diagnostics, capped at16 entries/4096 bytes; required/type checks reject before admission without consuming IDs. Existing strict validation, unknown properties, identity authority and safe points remain intact.
The actual installed Pi ToolDefinition/client/stdio proxy path preserved requiredness and omitted arguments against a separate production viewer importing ABeautifulGame:1 Queen_B transform,32 pawn transforms and1 fresh-handle reinspection, with0 non-read calls. No user viewer/controller/configuration/Scene mutation; no demonstrated omission loss or client adapter change.
Verified (workspace root; every runtime prepended `$PWD/_out/lib` to `LD_LIBRARY_PATH` and set workspace-local `TMPDIR`):
- `cmake --build _out/_cmake --config RelWithDebInfo -j6` then `cmake --install _out/_cmake --config RelWithDebInfo` → exits0, existing CUDA/UI/network/Lua/SDL3/integration/MCP/tests configuration retained.
- `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[request-guidance]'` →551 assertions/6 cases; corrected wildcard omission and wire-edit/restore scenario filters →97/133 assertions,1 case each. Earlier exact scenario selectors matched nothing and are not counted.
- `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` →68/68 pass (69.26s); integration binary `'[integration]'` →117985 assertions/317 cases in final observed run (polling makes totals variable).
- `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect --log-file=.scratch/viewport-integration/logs/01-valgrind.txt _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[request-guidance],[node-edit],[node-position]'` →exit0,7362 assertions/31 cases,0 errors and0 definite/indirect lost bytes.2376 possibly-lost bytes/15 blocks,4186630 reachable bytes/180 blocks and driver ioctl/address-range warnings remain disclosed; no suppressions, sanitizer or GPU-memory qualification claim.
- `python .scratch/viewport-integration/01-live-viewer.py` →three consecutive final passes; `python -m py_compile` on that fixture and `clang-format --dry-run --Werror` on all seven changed C++ files →exits0.
Evidence/source proof, raw-log pointers and limits: [01-verification.md](../01-verification.md), [01-live-proof.json](../01-live-proof.json), [01-outcome.json](../01-outcome.json). Parent/bookmark matched dispatched `ac9f90300553507e5902a6893b18ecce95a2abf5`; spec hash unchanged. No upstream gateway/model-generation test, barney/CUDA viewport parity or later-ticket qualification is claimed. **Final independent whole-spec review remains pending**, supplied by the orchestrator after merges; author inspection is not independent review.
Holes filed: none. No splits/new tickets. Exact base-workflow outcome JSON is committed in01-outcome.json.
