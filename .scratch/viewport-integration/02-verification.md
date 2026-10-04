# 02 — Truthful view identity and effective bindings

## Scope and lineage

Workspace: `/home/tarcila/Code/ANARI/vela/viewport-bindings`.
Dispatched parent and integration bookmark: `1ee298859b03a495b1543d057f08f0927b1e20c9`.
Verified the empty worker's exact parent and bookmark before source changes.
Spec SHA256: `e68c0e9d1329d0357388dceca18043b613abdcdf6389c49699d7562f348352ae`.
Read 02, the complete spec, executor/base/TDD workflow, prerequisite history,
and immutable camera-capture 20/14 contracts. No historical tracker, spec,
configuration, other workspace or integration bookmark was changed.

## Delivered behavior

- Host-assigned roles, bounded 256-byte names and opaque Integration-lifetime
  view identities. Removed slots are never reused; discovery skips retired
  slots rather than hiding later registrations. New Integration instances
  have distinct identity domains. Generic/headless/unknown forms remain valid.
- Authorized discovery and admitted view info share bounded effective source
  metadata: actual bound camera/renderer references, world pose/FOV conventions,
  actual Device Identifier/subtype, native dimensions, display dimensions,
  logical-unit scale and framebuffer scale, renderer/pipeline/color settings.
  Unknown backend defaults, quality samples and dynamic pipeline state have
  explicit reasons. Effective metadata never fabricates rendered provenance.
- Actual viewer snapshots use frame-bound previous camera/renderer, not UI
  selections. The owning-thread adapter performs no ANARI calls. An atomic
  transition gate publishes completed background device setup; switching,
  missing, inactive and unsupported custom-frame sources are unavailable.
  Normal host changes update configuration history, including camera/renderer
  Scene histories, dimensions, pipeline/exposure changes and teardown.
- The viewer registers a distinct legacy Helide AgentView plus its interactive
  viewport objects. Read-only viewport registrations explicitly opt out of
  scene-hook render confirmation. Roles themselves grant nothing.
- Finite host capture-policy registration/update remains capture/frame closed.
  Validation checks dimensions, quality declarations, count/deadline/resource
  relationships, every admitted aspect ratio against the proved PNG encoder
  envelope, and base64/duplicated metadata/response overhead. Invalid updates
  preserve policy and token; unchanged updates invent no freshness. The frame
  cache goal is 8 MiB, not an exposed or qualified cache implementation.
- Legacy render arguments, grants, dimensions and PNG path remain unchanged.
  Receipts label independent renders, preserve source identity/role and use
  image-producing metadata only with explicit backend attestation. Otherwise
  rendered configuration is unknown. AgentView attests its owned configuration;
  a retained receipt is unchanged after a real viewport resize.

## TDD and public evidence

Red artifacts are ignored local logs: absent role API; absent policy API;
real viewer missing viewport registration; six receipt assertions failing;
and two tool-description assertions failing. The first description name
filter matched zero cases, was not counted as evidence, and was corrected to
`*MCP effective binding*`, which matched one case and failed as expected.

Three new public headless cases use real Scenes, bound versus unrelated cameras,
renderer changes, a real Image Pipeline/pass, resize/pipeline changes, transition,
read denial, closure/recreation and cross-instance identity, bounded names,
twelve invalid policy variants, unchanged/update/change-back policy tokens.
Existing admitted/MCP tests additionally cover historical replay, all-target
read withdrawal, representation rejection and legacy PNG decoding/rendering.
The final focused selection matched nine positive cases, not an absent filter.

## Exact final verification

All shell commands ran in this workspace. Runtime test commands used:
`LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH"`
and `TMPDIR="$PWD/.scratch/viewport-integration/tmp"`.

1. `cmake --build _out/_cmake --config RelWithDebInfo -j3` — exit 0,
   warm-cache all-target build; no cold configure or disabled features.
2. `cmake --install _out/_cmake --config RelWithDebInfo --prefix "$PWD/_out"`
   — exit 0 before runtime verification. Apps use the freshly built workspace
   executables; installed workspace libraries are first in lookup.
3. `ctest --test-dir _out/_cmake -C RelWithDebInfo -j1 --output-on-failure`
   — 68/68 passed, 69.60 seconds; serialized to avoid fixture collisions.
4. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests '[view-info],[view-identity],A matching render*'`
   — 9 cases, 592 assertions passed.
5. `_out/_cmake/RelWithDebInfo/vsrIntegrationTests`
   — 320 cases, 118035 assertions passed in the final direct run.
6. `valgrind --error-exitcode=99 --leak-check=full --show-leak-kinds=definite,indirect --errors-for-leak-kinds=definite,indirect _out/_cmake/RelWithDebInfo/vsrIntegrationTests '[view-identity]'`
   — exit 0; 3 cases/142 assertions; 0 errors, 0 definite/indirect lost bytes.
   Possibly lost: 22128 bytes/169 blocks; still reachable: 23433077 bytes.
   Memcheck reported unhandled-ioctl/address-range warnings. This is not GPU
   instrumentation, a claim of zero process retention, or ASan/UBSan evidence.
7. `python .scratch/viewport-integration/02-live-viewer.py helide` and the same
   command with `barney` — both passed. Each creates its own authenticated Xvfb,
   private runtime/config directories and fresh real viewer/installed Pi MCP
   client/proxy, resizes only that fixture's SDL window, and stops its processes.
   Each measured native 758x229, displayed 1011x305.4340515 logical units;
   after resize, native 278x139, displayed 371x185.5, framebuffer scale [1,1].
   Bound camera and renderer handles resolved via public inspection. Helide
   additionally returned one legacy 640x360 PNG and an unchanged historical
   independent-render receipt after resize. No protected image bytes or
   credentials are stored in the proof records.
8. `clang-format --dry-run --Werror <all 18 changed C++ files>` — exit 0.
   Exact changed-file command and final logs are local under `logs/02-*`.

Cache confirms this source/install workspace, CUDA, interactive UI, integration,
MCP, networking, Lua and SDL3 enabled. Overlay remained at about 3.5 GiB free;
no other workspace output/cache was removed.

## Qualification limits and completion boundary

This resolves only 02's metadata/identity/policy/legacy-provenance behavior.
Real barney metadata and normal viewing were exercised, not viewport PNG parity,
GPU readback, frame observation, quality/sample convergence or fresh capture.
Genuine viewers exercised real startup and resize; camera/renderer changes,
transition and explicit registration retirement/recreation were exercised at
public headless seams. No claim of automated live UI device/camera editing is
made. Dynamic pipeline effects, backend defaults and private quality state
remain explicitly incomplete/unknown rather than falsely complete.
No user's viewer, controller, Scene, camera or config was touched.
Final whole-spec independent review and required frame/GPU qualification remain
pending with the outer integration workflow. Author inspection is not independent
review. The serial integration bookmark remains the dispatched parent for the
orchestrator to merge this one verified worker commit; the worker does not move it.
Holes filed: none. No new tickets.
