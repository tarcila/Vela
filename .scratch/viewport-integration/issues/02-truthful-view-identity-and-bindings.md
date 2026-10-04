# 02: Truthful view identity and effective bindings

**What to build:** Discovery, view info and image receipts distinguish real interactive viewports from independent previews and report the effective source configuration without disturbing viewing.
**Blocked by:** none
**Status:** resolved
**Milestone:** 1
**Specification:** [Truthful Views, Presented Frames and Safe Selection Translation](../spec.md), stories1–22; decisions2–4 and23–24.

## Current behavior

The sole viewer integration registration is an independent fixed640x360 Helide AgentView. A real barney viewport is visible but not separately registered, and AgentView view info reports missing binding. Source lineage already includes headless view-info and encoder/identity work. Existing camera-capture20/14 describe policy/real-binding work that this slice should reuse, not blindly duplicate or close.

## Acceptance criteria

- [x] Registered interactive/independent-agent views expose stable identities, bounded names and truthful role/origin; generic/headless/unknown hosts retain valid forms. Roles grant no rights or qualification. Recreated views cannot alias retired identities.
- [x] Authorized discovery and vsr_view_info report genuinely bound camera/renderer, actual Device Identifier/subtype, projection/pose conventions, native/display dimensions/scale, applicable renderer/Image Pipeline/color settings and matched scene/view/configuration/policy revisions. Distinguish current effective state from image-producing state; unknown/incomplete fields carry reasons and rendered provenance is never invented.
- [x] Provide validated bounded host policy information required for later frame observation while preserving capture-default unexposed state. Reuse relevant20/14 interfaces and proved PNG bound; do not imply completing separate fresh capture08 or its backend quality qualification.
- [x] Legacy render dimensions/arguments/grants/results/headless behavior remain unchanged; independent images carry enough provenance to prevent viewport mislabeling. New metadata performs no render/GPU query/readback/allocation, device fallback, camera reset or persistent write.
- [x] Camera/device/renderer/size/pipeline/exposure/lifecycle changes invalidate appropriate observations. Switching/teardown reports actual unknowns; cached/source images keep their rendered configuration rather than acquiring newer selection labels.
- [x] Own snapshots on the true viewer thread/safe-point interface, never read live UI state from transport threads or introduce upward library dependencies. Preserve Scene synchronization/permission/callback behavior.
- [x] Public headless and genuine viewer tests demonstrate distinct roles, bound versus unrelated camera, changed configuration, transition/closure/recreation, current read denial and bounded representations. Qualify a real isolated barney metadata path with measured dimensions, not hardcoded historical HUD sizes.
- [x] Fresh enabled build/install, focused plus full applicable regressions and formatting pass with actual counts/proof. No user's running viewer/configuration/controller is changed.

## Out of scope

Presented-frame pixels/observation, fresh capture08, translation, camera/renderer editing, source archives/Studio protocols, historical tracker mutation and backend qualification earned merely from metadata.

## Comments

Logically independent of01 after baseline, but both may touch common MCP interfaces; serialize writers unless proven disjoint. Final independent review is orchestrator-owned integration-stage work, not author self-review or a new route hole. Source module/policy decisions remain within the approved spec and existing immutable contracts.

### 2026-10-04 iteration — resolved
Added bounded names, non-reused opaque view identities, truthful roles/origins and matched discovery/view-info metadata, with validated finite policy updates and capture/frame exposure closed. Registered genuine viewer sources separately from AgentView using actual bound references, owning-thread snapshots, transition/unknown reasons and explicit scene-sync opt-out. Legacy PNG receipts preserve independent origin and explicitly attested historical configuration; unsupported producer provenance stays unknown.
Public headless tests cover real Scenes/pipeline, unrelated versus bound camera, renderer/configuration changes, transition, retirement/recreation, read denial and invalid/unchanged/change-back policy. Isolated genuine Helide and barney viewers resolved actual bound references and measured resize from native 758×229/display 1011×305.4340515 to native 278×139/display 371×185.5. Helide also preserved one historical independent 640×360 PNG receipt after resize. Detailed proof: [02-verification.md](../02-verification.md); no user's viewer/controller/config/Scene touched.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo -j3 && cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"` → enabled warm all-target build/install exit 0 before runtime tests, workspace-installed libraries first, local TMPDIR. `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure` → 68/68 passed (69.60 s). `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[view-info],[view-identity],A matching render*'` → 9 cases/592 assertions; full executable → 320 cases/118035 assertions. `clang-format --dry-run --Werror` over all 18 changed C++ files → exit 0.
Verified: `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[view-identity]'` → exit 0, 3 cases/142 assertions, 0 errors and 0 definite/indirect lost bytes; 22128 bytes possibly lost plus ioctl/address-range warnings, not GPU or sanitizer qualification. `python .scratch/viewport-integration/02-live-viewer.py helide` and `barney` → both passed via fixture-private real viewers and installed Pi client/proxy. Ignored raw logs remain local in `logs/02-*`.
Limits: metadata is qualified, not presented-frame pixels/readback, visual parity, runtime sample/quality convergence or fresh capture. Backend defaults and dynamic pipeline state remain explicitly incomplete/unknown. Live UI camera/device editing was not automated; configuration/transition/lifecycle coverage additionally uses public headless seams. Final whole-spec independent review and real frame/GPU qualification remain pending with the outer orchestrator; this is not a whole-spec completion claim. Source parent and integration bookmark were reconfirmed at the dispatched base; the worker leaves the bookmark unchanged for the serial merge.
Holes filed: none. No new tickets. Retry WIP: none.
