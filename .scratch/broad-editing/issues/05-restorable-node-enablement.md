# 05 — Enable or disable nodes in restorable atomic batches

**What to build:** Show/hide existing nodes through the complete authorized node-edit path, with truthful activity, synchronization, and exact conditional restoration.

**Blocked by:** 28
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 1, 5, 7, 8, and 9 for enabled-state edits; Testing Decisions 7–12 as applicable.

## Current behavior

Nodes can be inspected and versioned, but Agent Edit Batches and restoration journals currently target only supported object parameters.

## Acceptance criteria

- [x] Scene registration explicitly opts into node edits and discovery advertises exposure; existing object edit grants alone do not widen node writes. Viewer agent-integration mode opts in.
- [x] Compatible node-discriminated edit entries accept only a Boolean enabled value on an existing node of any supported node kind, with safe identity/version/structural checks.
- [x] Batch limits, permissions, replay identity, current-state validation, preparation failure, and no-partial-commit rules match existing object edits; unrelated sibling writes do not conflict.
- [x] Complete state is visible before coalesced normal layer notifications; exactly one ordinary scene-sync acknowledgement follows. Committed sync failures and post-commit cancellation are reported honestly.
- [x] Commit results include Batch Reference, scene revision, and node_versions/dependencies without manufacturing object tokens.
- [x] Journal/activity budgets cover node state. Restore recovers only the changed flag, detects subsequent relevant writes, preserves unrelated state/defaults, and follows permanent conflict, one-way consumption, expiry, and replay rules.
- [x] Local/external Restore, Disable agent edits, Disconnect, and Resynchronize retain their existing authority and lifecycle behavior.
- [x] Headless and MCP tests plus a thin viewer check demonstrate enable → inspect → restore; all existing object-only tests remain green.

## Out of scope

Transform edits, layer activation changes, structural editing, broader object fields, and journal/result-store redesign.

## Comments

### 2026-10-02 iteration — resolved
Salvaged the ten code files from latest cumulative WIP `puvqouxlmlqwkrouyzrxnyvkkkprloox` (not older `wmrwonkp`) onto recovery `ozkyuqkywynwqrqxulvtwyukyulutnpm`, then merged integration `44cad2b4` without conflicts; 04 world/SRT observations survive and pass the combined regression. Deliverable: `vvnuxoxrqsxwokorxmorkkosvnorzxsm`.
Finished explicit node exposure, enabled-only atomic admission/commit, narrow identity/edit/structural checks, shared bounded journals, honest sync outcomes and conditional flag-only restoration; documented actual `node_edits_exposed`, required `structural_version`, and returned `node_versions`. Added a thin ImGui-free viewer-model node activity/Restore check after Disconnect and Disable. Viewer compiled with existing cache, SDL3 ON/CUDA OFF; no MultiDeviceViewport source changes. No graphical/imported-asset smoke or unrelated disabled-feature configuration is claimed.
Verified: `cmake -S . -B _out/_cmake -DVSR_BUILD_INTERACTIVE_APPS=ON -DVSR_USE_SDL3=ON -DVSR_USE_CUDA=OFF` → passed; `cmake --build _out/_cmake --target vsrViewer vsrIntegrationTests vsrMcpProxy anari_library_vsr -j 4 && cmake --install _out/_cmake` → passed; `_out/_cmake/vsrIntegrationTests '[node-edit]'` → 4,481 assertions/10 cases; `_out/_cmake/vsrIntegrationTests` → 13,632 assertions/222 cases; `ctest --test-dir _out/_cmake --output-on-failure` → 37/37 passed. `clang-format --dry-run --Werror` on all ten changed C++ files → passed. Exact final logs: `.scratch/broad-editing-logs/05-finish-viewer-{build,install}.log`, `05-finish-node.log`, `05-finish-all.log`, `05-finish-all-ctest.log`. Assertion totals vary with timing-dependent existing cases; case totals above are final reruns.
Holes filed: none this continuation. Existing nonblocking `73-node-structural-dependency-envelope` remains durable `needs-triage`: wire-name and 4096-step-cap assumptions, not waived structural checks or weakened acceptance.


### Orchestrator recovery — interrupted continuation

Enabled-state focused tests now pass (4460 assertions, nine cases), but the
worker again reached the stdout-event limit before finishing the outcome.
Viewer compilation was attempted with interactive apps enabled but SDL3 still
disabled; the resulting missing CopyToSDLTexturePass errors require correcting
the verification configuration, not unrelated source changes. Preserve this
new cumulative code WIP separately from the first parked attempt, then complete
full regression, viewer compilation, docs, and committed status.

### Orchestrator recovery — interrupted enablement pass

The worker hit the stdout-event limit after enabled-only code and an initial
passing edit/restore scenario. Broad authority/atomicity/retention/MCP/viewer
verification is not yet complete, so no resolved state is claimed. Preserve
code-only WIP and cheap-assumption ticket73 before a bounded continuation.

### Orchestrator dependency update

03 was split before implementation into 25–28. This ticket waits for terminal
replacement 28, so it cannot ship editable nodes with partial identity,
change-back, or ancestry safety. No edit/restoration requirement was weakened.
