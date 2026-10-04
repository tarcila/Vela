# Viewport integration — final acceptance

## Accepted range

- Approved clean base: `888e173a4bfd430b598fba23a7446b6bb990ef20`.
- Verified implementation tip: `70a795672947bcfb9c013b0630aa2c4466688c81` (`viewport-integration`).
- Frozen specification SHA256: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`, unchanged.
- All six approved feature tickets are resolved; both frame and translation chains are integrated. No remote publication or historical ticket closure is inferred.

## Independent verification

The merger independently built and installed the enabled CUDA/UI/network/Lua/SDL3/integration/MCP configuration and ran:

| Gate | Result |
|---|---|
| Full CTest | 68/68 |
| Full integration | 364 cases / 127,714 assertions |
| Translation commit | 9 cases / 2,142 assertions |
| Preview + commit | 18 cases / 3,275 assertions |
| Edit/restore/canonical MCP union | 71 cases / 24,621 assertions |
| Cumulative feature union | 58 cases / 10,764 assertions |
| Targeted CPU Memcheck | 43 cases / 10,086 assertions; zero errors and definite/indirect leaks |

Possibly lost bytes and driver warnings remain disclosed; these are not a clean universal GUI/GPU sanitizer claim. Poll-dependent assertion totals vary between passes.

Independent frame acceptance additionally ran two production barney and two production Helide lifecycle sequences: 124 independently compared parity PNGs and four fresh close/reopen images, zero busy retries, application/presentation receipts audited. Fragmented-control old-oracle RED/current GREEN, destructive two-view reconstruction, maximum-size pressure and actual slow HTTP drain tests passed. The earlier withheld acceptance and both intermittent failures remain recorded, not overwritten by later passes.

Author evidence and independent merger reruns are distinguished in [merge04r6-verification.md](logs/merge04r6-verification.md) and [merge06-verification.md](logs/merge06-verification.md). Positive CUDA allocation/migration/call attribution was inspected from the preserved author trace/audit, not independently rerun by the merger or source reviewers.

## Independent review

Two separate fresh reviewers examined Standards and Spec against the frozen 49-file source/documentation range (32 commits, 13,086 diff lines):

- [Standards](final-standards-review.md): zero required documented-standard violations; three optional judgement calls (repeated edit-entry construction, repeated response-size literal, TranslationService size). These are not bugs or required fixes; broad policy/global-map refactors remain deferred.
- [Spec](final-spec-review.md): sampled source review approved, zero required findings.
- [Coverage follow-up](final-spec-coverage.md): all 120 stories accounted for, classifications distinguish directly inspected source from inspected ticket/test-log evidence and partial inspection. Roughly 35 high-risk stories directly re-inspected; the majority rely on inspected evidence. This is not an exhaustive independent re-derivation of affine math, PNG/base64 arithmetic, every assertion body, or the accumulated older trunk. Future larger-resolution policy remains explicitly future scope, not an absent current requirement.

No required findings were raised, so no source-fix dispatch is invented. The final ticket06 integration-stage checkbox can close on this evidence, with these coverage limits retained.

## Supported profile and exclusions

Presented-frame retrieval is qualified for the instance-bound `SDL_CPU_READY_V1` normal HOST/MANAGED-host CPU-ready SDL upload path, including actual production barney and Helide. Non-unit SDL/framebuffer scale, unknown storage/interop/output/effect profiles remain unavailable. Fractional render-resolution scale is a different quantity and remains truthfully reported. Latest means actually display-submitted at the request safe point; historical displayed output is not replaced by a fresh independent render. Images exclude application chrome/HUD/gizmos.

Unchanged caps: 2048 per axis, 2,073,600 pixels, 8 MiB PNG, 12 MiB response, 16 MiB proxy and 256 MiB owned budgets. CUDA on/off tracing and complete CPU ownership attribution support the bounded normal-display acquisition path, not whole-process RSS/VRAM or arbitrary-driver guarantees. Matching external GUI error contexts/DBus leak, unsupported OptiX Compute Sanitizer and maximum Massif timeout remain disclosed failures.

Fresh one-off queen's-eye capture, confirmed-sample rendering and 8K-plus output are separate future contracts, not delivered by this work. The user's original viewer/controller, Scene and configuration were not restarted or changed; isolated instances supplied runtime evidence.

## Workflow and cleanup

12 implementation dispatches and seven merger dispatches were used; one merger withheld acceptance before causal fixture correction and fresh independent reruns. Task-specific 60-minute working/75-minute hard budgets applied; normal agents were unchanged. Build-cache reclamation affected only verified completed owned worker `_out` trees. Source/proof commits remain in Jujutsu. Owned worker scratch evidence is archived locally before workspace cleanup; integration workspace and unrelated workspaces remain intact. No push, issue publication or PR was authorized or performed.
