# 75 — Address final review without expanding feature scope

**What to build:** Preserve the completed broad-editing behavior while making the new registry choice explicit, transform issue states typed, and node field preparation easier to verify.

**Blocked by:** 12
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md); existing behavior must remain unchanged.

## Review scope

The final Standards review requested changes for a new node-identity map without documented container rationale, numeric transform issue codes, and difficult-to-read nested node field preparation. The separate Spec/correctness review found no confirmed defects; its optional admission/exposure question must be documented, not used to weaken live validation.

## Acceptance criteria

- [x] Evaluate the new node serial registry's actual scene-sized cardinality and lookup requirements. Use an appropriate VSR primitive where suitable or document a grounded reason for retaining logarithmic lookup; do not undertake the deferred legacy map migration.
- [x] Replace untyped numeric transform issue states with a named enum, preserving all returned reasons, invalid-state behavior, and authenticated cursor encoding/validation.
- [x] Extract small per-field preparation/validation helpers where they clarify the existing atomic grouped planner, preserving order independence, final-parent world conversion, exact cached state/defaults, replay identity, and post-write restoration dependencies. Do not create a speculative class hierarchy or shared result store.
- [x] Record dispositions for optional description-density, continuation-state, and tiny overflow-guard duplication comments. Do not remove model-visible requirements needed to form valid requests or add unrequested architectural work merely to satisfy a heuristic.
- [x] Preserve live node exposure/permission validation before mutation and document why static envelope shape and safe-point authorization are distinct. No synchronous-admission rejection contract is invented.
- [x] Existing public headless/MCP chess, grouped/SRT/world/mixed/bounded-name cases, full build/install/CTest, formatting, and relevant disabled configurations remain green. No user-local files or runtime artifacts enter commits.

## Out of scope

New edit capabilities, spec changes, broad container migration, result-store extraction, new protocol authority, and remote publication.

## Comments

Authoritative review record: `/home/tarcila/Code/ANARI/vela/broad-editing/.scratch/broad-editing-logs/review-notes.md` (frozen source `622956eb -> 9223e39a`; adjacent code-only patch/file/commit lists). This is the single implementation ticket for all grounded final-review follow-ups; both review axes retain their original verdicts and limits.

### Review dispositions

- **Standards: registry rationale addressed.** `Identity.hpp` documents retention of every issued `(layer, node lifetime)` pair for the SceneIdentity lifetime, including removed nodes. Cardinality grows with scene size and churn, not page/batch limits; budgeted traversal calls `handleOf()` repeatedly. The actual `FlatMap` implementation uses linear lookup, unsuitable for this growing history. Retain `std::map` for O(log N) lookup/insertion without rehash spikes; ordered iteration is not required. No legacy container migration.
- **Standards: typed transform issues addressed.** `Inspector::TransformIssue` names NONE/NON_FINITE/NON_AFFINE/ARRAY_ANCESTRY/WORLD_OVERFLOW with explicit uint64 values 0–4. Preserve exact wire strings `non_finite_transform`, `non_affine_transform`, `transform_array_ancestry`, and `world_transform_overflow`, invalid-state precedence and authenticated cursor encoding/validation. The new one-visit continuation test checks all reasons and available states through resumed pages; the source audit checks the unchanged cursor body apart from carrier casts.
- **Standards: preparation readability addressed.** Small `preparePositionField` and `prepareTransformField` helpers complement existing `prepareSRTField`/`prepareMatrix`; matrix preparation decomposes only the cache, never reconstructing the authoritative matrix. Keep one atomic grouped planner and its order-independent final-parent world conversion, exact cached state/defaults, replay identity, restoration payloads and post-write dependencies. No obsolete helper, hierarchy or shared result store.
- **Optional description density: retain intentionally.** MCP descriptions are the model-visible contract for supported values, opt-in, preconditions, matrix conventions and budgets; shortening away requirements would regress valid-request discoverability. No protocol/schema changes.
- **Optional continuation state: retain intentionally.** The bounded authenticated, kind-dependent Position struct preserves resumable work without an ancestor stack; splitting it or extracting a result store is unrequested architecture, not a confirmed defect.
- **Optional tiny overflow-guard duplication: retain intentionally.** Three short guards remain local to their arithmetic/budget paths; a speculative shared abstraction would obscure their safety conditions without changing behavior.
- **Spec/correctness: approve remains unchanged.** The optional admission/exposure question is not a confirmed defect. Static envelope checks establish shape; exposure, current permission and live targets can change after admission, so safe-point checks deliberately reject before any mutation. The added planner comment records this distinction. No invented synchronous-admission rejection contract or weakened authorization.

### Verification and limits

Existing ASCII proofs are workspace-local under `.scratch/broad-editing-logs/75-*`; `75-verify.sh` records exact commands and `75-verify.exit` is 0. Its rebuild after edits reported `ninja: no work to do`, then installation preceded tests with `LD_LIBRARY_PATH="$PWD/_out/lib:${LD_LIBRARY_PATH:-}"` to avoid stale installed libraries.

- `cmake --build _out/_cmake --parallel 4`; `cmake --install _out/_cmake`; `ctest --test-dir _out/_cmake --output-on-failure --parallel 4`: **68/68**. Current cache is Release, CUDA OFF, apps/interactive apps ON, networking ON, integration/MCP ON (CPU UI/network build).
- `_out/_cmake/vsrIntegrationTests '[chess]'`: **6233 assertions / 1 case**; `'[MCPchess]'`: **2666 / 1**.
- `_out/_cmake/vsrIntegrationTests '[node-srt],[node-world],[mixed-edit],[bounded-names],[node-transforms]'`: **6897 assertions / 27 cases**, including grouped/SRT/world/mixed/bounded-name coverage.
- Disabled configurations use Ninja Release, CUDA/apps/interactive/UI/networking/Lua OFF, MCP OFF, with integration respectively OFF/ON; exact configure flags and build/install/CTest commands are in `75-verify.sh`. Installed-library CTest: integration OFF **35/35**, MCP OFF **36/36**.
- Closeout reran `python .scratch/broad-editing-logs/75-source-audit.py`: **PASS** for unchanged SRT/matrix fidelity, cached state, structure/ancestry, replay, final-parent order, exact restore and cursor body.
- Closeout reran `clang-format --dry-run --Werror` on all four changed source files (clang-format 21.1.8): **PASS**. Also reran the combined chess/MCP/focused filter above against the installed library: **15796 assertions / 29 cases**, all passed.

Limits: runtime proofs cover these CPU/configuration suites, not GPU rendering, exhaustive security testing or a fresh multi-family panel. Original reviews were separate static Standards and Spec workers; no clean panel gate is claimed. Only the four source files and this ticket belong in the commit; logs, scripts, runtime artifacts, user-local files, spec and sibling workspaces remain excluded.
