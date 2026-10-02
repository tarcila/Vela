# 06 — Move transform nodes locally and restore exactly

**What to build:** Move one or several existing transform placements in local space while preserving their linear matrix components and enabling exact batch restoration.

**Blocked by:** 05
**Status:** resolved
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 5–9 for local position; Testing Decisions 5, 7–10, and 12.

## Current behavior

The enabled-state slice provides node admission, version checks, atomic commit, synchronization, activity, and restoration. Position is still outside the node edit surface.

## Acceptance criteria

- [x] Local position accepts exactly three finite representable components; omitted space means local. Unsupported node kinds and malformed values reject without changing any target.
- [x] Replace only the translation column; preserve every other stored matrix component, including shear/reflection/degeneracy, and preserve default/reset behavior.
- [x] Multi-node position batches and position-plus-enabled groups are atomic, bounded, version pinned, independent of entry order, and restorable.
- [x] Later target writes, change-back, reparenting, ancestor removal, and identity replacement conflict. Unrelated siblings and parent transform changes alone do not block local-space edits.
- [x] Preparation failure emits no mutation notifications or committed journal. Ordinary notifications/synchronization and result/version reporting use the preceding complete node path.
- [x] Restore exact prior transform/representation for changed fields only; keep unrelated state and existing conflict/retention/authority semantics.
- [x] Headless and thin MCP tests demonstrate movement and restoration with independently worked positions and nontrivial original matrices.

## Out of scope

World positioning, SRT/full-matrix replacement, structural editing, camera/view movement, and broader object writes.

## Comments

### Orchestrator recovery — interrupted local-position pass

The worker hit the stdout-event limit after initial implementation and one
passing red/green scenario. Complete grouped-field, fine-conflict, exact matrix/
cache/default restoration, MCP, and full regression verification before marking
resolved. Code-only WIP is parked; logs are in broad-editing-logs4.

### 2026-10-02 iteration — resolved
Salvaged all seven paths from WIP `xtyrltplpntrlsumknlylqsvrzuyxkuo` on the clean failure parent; completed position/enabled grouping, multi-node final-state notifications and exact matrix/SRT/default restoration. Extended headless coverage for preparation faults, narrow conflicts/reparenting/ancestor removal, invalid and unsupported values, authority/synchronization, replay, restoration eligibility and journal expiration; added thin MCP inspect/grouped-edit/restore and documentation.
Deliverable: `qrquxqluqorltklpnqxovzmnsrwqovyn`. No worker07 or integration-bookmark changes. The merger should unify this exact-state `setAsTransform(Mat4, Mat3)` overload with worker07's independent `setTransformState(Mat4, Mat3)`; retain default baselines and write history.
Verified: `cmake --build _out/_cmake --target vsrIntegrationTests -j 4 && _out/_cmake/vsrIntegrationTests '[node-edit],[node-position]'` → passed, 5,118 assertions/16 cases. `cmake --build _out/_cmake -j 4` → passed. `cmake --install _out/_cmake --prefix "$PWD/_out"` → passed (plain install initially failed because cached prefix is `/usr/local`; no privileged write attempted).
Verified: `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" _out/_cmake/vsrIntegrationTests` → passed, 14,507 assertions/228 cases; `LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" ctest --test-dir _out/_cmake --output-on-failure -j 4` → 36/36 passed. `clang-format --dry-run --Werror src/tests/integration/test_IntegrationEdit.cpp src/tests/integration/test_IntegrationMcpTools.cpp src/vsr/integration/edit/AgentEdit.hpp src/vsr/integration/mcp/tools/EditTools.hpp src/vsr/integration/restore/BatchRestore.hpp src/vsr/integration/values/EditableValues.hpp src/vsr/scene/LayerNodeData.cpp src/vsr/scene/LayerNodeData.hpp` → passed. Logs: `.scratch/broad-editing-logs4/06-cont-*` (outside task subtree). Existing cache has interactive apps disabled; no configure or GUI claim.
Holes filed: none
