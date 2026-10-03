# Ticket01 implementation and verification handoff

Date: 2026-10-03. Workspace: `/home/tarcila/Code/ANARI/vela/broad-editing`.
Dispatch change: `xrwvlnwz`; parent: `uwnoqyvl`.

## Disposition

The payload/owner foundation is implemented and its automated checks pass. Ticket01 remains **ready-for-human**, not resolved: the required independent correctness/Standards review has not happened. No review channel is exposed to this executor, and dispatching other agents is forbidden. Static analysis and implementer inspection are not relabeled as independent human/agent review. Ticket05 records that execution-route hole. Ticket04 records an unrelated pre-existing missing public activation overload without repairing it.

This is not whole-Scene bounds qualification. Automatic membership validity belongs to02 and transfer validity/cumulative qualification to03. No bounds computation, capture, encoder, limits, UI interoperability or ANARI Device capture qualification is claimed.

## Implementation boundary

- Scene owns one fixed dependency state with a checked monotonic counter and a revocable Scene pointer. Layer owns one lifetime control block, initialized before constructing its root. Both survivors use weak references; neither keeps Scene or Layer alive.
- Detached values resolve objects through the Scene state independently of their former Layer. Normal Layer destruction revokes its Layer pointer; Scene teardown revokes resolution before destroying any object pool. Old weak ownership cannot be rebound through allocation reuse.
- Public attached payload writes advance dependency state without delivering new coarse UpdateDelegate callbacks. Instance set/replace/clear advances dependency validity but not precise edit/transform history.
- Copy/move assignment preserves receiving attachment. Genuine move-out is detached, invalidates the source, and leaves the live source attached for later edits. Unsupported foreign object-index assignments, including instance references, reject before mutation.
- Forest has an optional, Scene-independent receiving-value binding callback. It runs for stored root/ordinary/chosen-slot values, not gaps, failures or reserve. Optional storage traits distinguish relocation/slot replacement from editable payload moves; attachment, values and precise histories survive reserve and append growth. Default generic Forest/Data Tree behavior stays covered by existing tests.
- Both public revision getters fold the dependency counter into **both** domains in O(1), independently per SceneIdentity registration. No Scene fingerprint, traversal, growing observation history or always-changing global dependency clock is used. Existing precise transform clocks are unchanged. Revision counter exhaustion terminates instead of reviving a token.
- Default restoration now releases an intervening object reference using the established use-count release path; the pre-existing missing decrement was reproduced red before repair.

## Requirement-to-evidence map

All new primary cases are in `src/tests/integration/test_IntegrationIdentity.cpp`, through actual public Scenes, Layers, payloads, Forest APIs and SceneIdentity revisions, without private epoch access or added manual signals in automatic cases.

| Ticket01 requirement | Positive/negative public evidence |
| --- | --- |
| Local/ancestor matrices, explicit default, restoration, SRT, exact state, enabled/ancestor enablement, object/type-index/array references, raw/scalar/empty/null values, instance set/replace/clear | `attached payload writes invalidate both observations without signals`; both getter orders, change-back, settled polling, independent Scene and second registration, node lifetime/topology, no new coarse callbacks |
| Detached operations, rejected lookup/helper insertion, failed occupied/invalid chosen slot, root ownership, reserve negative controls | `detached work and storage reserve are not attached edits` |
| Root/ordinary first/last/chosen-next/chosen-high/gap/reuse ownership; exact object counts and source token stability through append/reserve | `ordinary chosen gap and reused storage bind safe payloads` |
| Values, defaults, enabled state, name, instance values, exact matrix/cache pair, local/ancestor histories survive both append and reserve | `storage relocation preserves exact payload and ancestor history` |
| Retained object-containing Layer/root after actual Scene destruction; detached reference after actual Layer destruction with live balanced Scene counts; both owners gone; Scene/Layer storage reuse | `retained Layers and detached payloads qualify both owner lifetimes`; uses public shared Layer retention and optional owner destruction/re-emplacement, not invalid NodeRef dereferences |
| Attached source/detached/dead-source-Layer copies, live/detached moves, self-assignment, destination resolution and later source/destination edits | `attached assignment keeps the receiving Layer and exact reference counts` |
| Foreign payload/instance indices never reinterpret destination pools | `attached assignment never reinterprets foreign object indices`; rejected work preserves values, precise history, counts and revisions |
| Genuine move-out versus relocation and subsequent editable source | `genuine move-out is detached but the changed source stays attached` |
| Existing object/reference/array-write-unmap/active-Layer/filter invalidation | `existing object array active Layer and filter dependencies remain valid`; both getter orders |
| Explicit controls, structural batching versus update-batch delivery | `automatic dependencies preserve explicit signals and distinct batches` |
| Optional generic binding and default-neutral storage/container semantics | New `Forest value binding is optional and only binds stored members` in `test_Forest.cpp`, plus existing Forest/Data Tree/Data Path/ObjectPool suites |
| Precise local/ancestor identity, grouped/mixed/local/world edits and conditional restoration, metadata, authority/suppression and historical replay | Full unchanged integration regressions, including node-version/node-dependency, IntegrationEdit, IntegrationRestore, IntegrationSync and IntegrationInspection cases |

Membership validity assertions for successful insertion/erase/clear/removal and subtree transfer are deliberately not represented as completed01 behavior. Storage safety for those supported insertion paths is tested here.

## Red evidence

All build/install/test invocations used the existing enabled `_out/_cmake` tree and workspace-local logs.

1. Build `vsrIntegrationTests`, install, run `_out/_cmake/vsrIntegrationTests '[payload-validity]'`: `logs/01-red-payload.txt` records **1 matched case / 4 assertions; 2 failed assertions** for unchanged SceneRevision and InspectionRevision after a raw transform write (exit2). The same public mutation is green without a signal workaround.
2. Build/install then run the expanded `[payload-validity]` filter: `logs/01-red-default-count.txt` records **6 matched cases / 972 assertions; 2 failures**, one per getter-order run, for the leaked reference on default restoration (exit2). Correct release makes both positive.
3. Build/install then run `'Scenario: attached assignment never reinterprets foreign object indices'`: `logs/01-red-foreign-tests.txt` records **1 matched case / 16 assertions; 8 failures** for accepted foreign instance indices (exit8). The object-valued source control already rejected; instance-index rejection is now green too.

Compilation errors encountered while developing tests are not counted as behavioral red evidence. The pre-existing pointer activation linker failure is in `logs/01-red-foreign-build.txt` and ticket04.

## Final automated verification

Every shell invocation was prefixed with `cd /home/tarcila/Code/ANARI/vela/broad-editing &&`. Exact executable/argument portions:

- `cmake --build _out/_cmake -j 6` then `cmake --install _out/_cmake`: exit0. Full enabled libraries/applications/tests rebuilt; changed installable `libanari_library_vsr.so` installed. VSR Scene/integration are static libraries linked into the freshly built executables; they do not have separate stale shared-library copies to install. Final build/install logs: `logs/01-final-build.txt`, `logs/01-final-install.txt`.
- `_out/_cmake/vsrIntegrationTests '[payload-validity],[payload-ownership]'`: exit0, **10 matched cases / 1,201 passed assertions**, `logs/01-final-focused.txt`.
- `_out/_cmake/vsrIntegrationTests '[integration]'`: exit0, **291 matched cases / 109,853 passed assertions**, `logs/01-final-integration.txt`.
- `_out/_cmake/vsrTests '[Scene],[Forest],[DataTree],[DataPath],[ObjectPool],[Object],[Array],[Parameter],[RenderIndex]'`: exit0, **50 matched cases / 1,059 passed assertions**, `logs/01-final-core.txt`.
- `ctest --test-dir _out/_cmake --output-on-failure`: exit0, **68/68 passed**, `logs/01-final-ctest.txt`.
- `valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=possible --errors-for-leak-kinds=definite _out/_cmake/vsrIntegrationTests '[payload-validity],[payload-ownership]'`: exit0, **10 matched cases / 1,201 passed assertions**, **0 errors / 0 contexts**, **0 definitely or indirectly lost bytes**, `logs/01-final-valgrind.txt`. No suppression file was used. Valgrind also reports 2,376 possibly lost bytes in15 blocks and 4,186,611 reachable bytes; possible-loss stacks run through NVIDIA libcuda initialization/thread TLS. These are disclosed, not called a completely leak-free process. Valgrind warns about an unsupported driver ioctl and large GPU address ranges; this is host owner/storage checking, not GPU memory/capture qualification. No configured sanitizer build tree exists here, and no cold sanitizer tree or disabled-CUDA configuration was created.
- `clang-tidy -p _out/_cmake src/vsr/scene/LayerNodeData.cpp src/vsr/scene/Layer.cpp src/vsr/scene/Scene.cpp src/tests/integration/test_IntegrationIdentity.cpp --checks='-*,clang-analyzer-*'`: exit0, all4 translation units processed with **no diagnostics**, `logs/01-final-clang-analyzer.txt`. This is independent automated static checking, **not** the outstanding independent correctness/Standards review.
- `clang-format --dry-run --Werror src/vsr/core/Forest.hpp src/vsr/scene/SceneDependencyState.hpp src/vsr/scene/Layer.hpp src/vsr/scene/Layer.cpp src/vsr/scene/LayerNodeData.hpp src/vsr/scene/LayerNodeData.cpp src/vsr/scene/Scene.hpp src/vsr/scene/Scene.cpp src/vsr/integration/identity/Identity.hpp src/tests/integration/test_IntegrationIdentity.cpp src/tests/test_Forest.cpp`: exit0, no diagnostics, `logs/01-final-format.txt`.
- `sha256sum _out/lib/libanari_library_vsr.so _out/_cmake/libanari_library_vsr.so`: both `5f2f7c18b3b0713a1d7d8cab7f451e3944fb634abcd72e61319d402d229ac6db`, confirming installed/build device parity after the final build.

Existing cache remains CUDA/UI/network/Lua/SDL3 enabled. No new configuration, other workspace, user-local configuration, screenshot, historical ticket, immutable spec/addendum, encoder code or orchestrator ledger was changed. Logs are ignored workspace-local bounded text, not image/base64/profiler artifacts.

## Remaining gate and resume

Independent reviewer must inspect ownership/teardown, attachment-versus-resolution, storage traits/default-neutral generic behavior, object-count transitions and coherent bounded revision folding against the frozen contract and coding standards. The implementation above has been inspected by its author; that does not satisfy independence. Resolve ticket05 with actual reviewer evidence and any requested verified corrections before resolving01 or dispatching02. Do not use the passing automatic tests to bypass that gate or to claim complete bounds validity.

The scoped executor declares deadline4500 and a60-minute working budget. `PI_SUBAGENT_DEPTH=1`; no deadline environment field was exposed, so no separate actual enforcement field is asserted. Normal executor/configuration was not edited. Work is being preserved within the authorized working budget, not split to the superseded20-minute limit.
