# 25 — Inspect live layers with safe layer and root identities

**What to build:** A complete public/MCP layer observation slice, with bounded queries and genuine layer/root lifetimes, not a hierarchy snapshot or a name-based identity.

**Blocked by:** 01
**Status:** wontfix
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 4; Testing Decisions 2, 3, 4, and 12.
**Files:** src/vsr/integration/identity/Identity.hpp, src/vsr/integration/inspection/Inspection.hpp, src/vsr/integration/mcp/tools/QueryTools.hpp, src/vsr/integration/mcp/README.md, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/core/Forest.hpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp, src/tests/integration/test_IntegrationMcpTools.cpp

## Current behavior

Split from 03 before implementation. IdentityAuthority/SceneIdentity expose object identities only. Inspection and MCP queries implement bounded object queries with filters from completed 01. Layer provides a root backed by Forest; removal and layer notifications exist, but roots are not externally addressable.

## Acceptance criteria

- [ ] Add compatible inspection `kind` dispatch: omission remains `objects`; implement `layers` without changing object results, permissions, admission, historical replay, or paging policy. Advertise only modes actually implemented at this stage; 26 adds `nodes`.
- [ ] Layer records expose host-issued opaque layer handle, real stored name, active flag, and opaque root-node handle. Object/layer/node handle domains cannot alias. Root identity is already lifetime-safe even though node records arrive in 26.
- [ ] Layer queries support exact name, literal case-sensitive name substring, and exact layer handle, intersecting filters. Reject object/node-only filters and malformed/wrong-kind handles; stale exact handles conflict, no matches succeed empty.
- [ ] Preserve scene layer order and all shared page/item/optional/string/visit/slice/scratch/deadline limits. Bind cursors to kind, all filters, policy, scene/launch/revision, and continuation position. No full hierarchy snapshot; sparse scans yield/terminate boundedly.
- [ ] Removing/recreating a same-name layer or resetting/replacing a scene invalidates old layer/root handles permanently, including between queries. Root reconstruction/clear cannot resurrect its prior identity. Do not rely solely on pointer/slot/name/final-value fingerprints or query-time comparison.
- [ ] Layer activation/name/root/membership observations correctly stale cursors and render-relevant changes advance scene revisions. Unchanged live handles remain stable through ordinary notifications.
- [ ] Public headless tests with real Scenes demonstrate multiple/inactive/duplicate-name layers, direct queries, filtering, paging/cursor rejection, ABA/reset/replacement, and wrong-kind handles. Thin MCP tests verify schema/parsing/returned layer fields; existing integration tests pass.

## Implementation and verification guidance

Use the confirmed public headless Integration safe-point seam, with one red/green behavior at a time; no new test-only hooks. Add the minimum core lifetime support needed for root correctness, reusable by 26, without implementing general node observations or speculative edits. Existing entry points include Layer::clear and Forest lifetime operations; inspect their actual semantics rather than assuming clear preserves or replaces the root.

Reuse an existing build, or use the minimal configuration in progress.md for a fresh workspace; build with at most four jobs. Capture verbose logs outside the task subtree under `.scratch/broad-editing-logs/`. Run changed headless/MCP cases and the full `vsr::integration` ctest entry, plus formatting for changed C++ files; record exact commands and results.

## Out of scope

General node records/filters (26), fine editable versions (27), ancestry tokens (28), world/SRT derivation (04), edits/restoration (05 onward), structural editing APIs, archive IDs, and unrelated refactors.

## Comments

### 2026-10-02 split from 03
First vertical observational slice. Its root handles must be genuine live identities, not placeholders. The complete replacement chain is 25 → 26 → 27 → 28; downstream 04/05 must wait for 28, not this slice alone.

### 2026-10-02 iteration — wontfix
split into 31, 32 before code: genuine layer/root lifetimes plus bounded unfiltered/direct headless queries → name filters, thin MCP, and combined original-contract verification. The full core/public/MCP acceptance surface is too large to safely implement and verify in this sitting; no partial implementation or resolved criterion is claimed.
Inspected actual Forest semantics: clear/erase(root) preserve the live root while erasing descendants; Layer::clear currently does not notify on its own. Parts explicitly preserve this behavior and unchanged identities through ordinary notifications, while requiring lifetime participation and permanent ABA/reset/replacement invalidation. No snapshot/pointer/raw-index fallback is authorized.
Orchestrator action: reblock 26 on terminal replacement 32 before dispatch; this worker did not edit sibling tickets or progress. Prior work was read from integration base `27911300`; retry WIP was none. No deliverable change.
Verified: `~/.local/share/agent-hooks/scratch-commit.sh preflight broad-editing` → passed after explicitly tracking both new tickets; `python3 -c 'from pathlib import Path; p=Path(".scratch/broad-editing/issues"); parts=[next(p.glob(str(n)+"-*.md")).read_text() for n in (31,32)]; assert all("**Status:** ready-for-agent" in s and "**Milestone:** 1" in s and "**Files:**" in s and "## Acceptance criteria" in s for s in parts); assert all("**Blocked by:** "+b+"\n" in s for s,b in zip(parts,["01","31"])); original=next(p.glob("25-*.md")).read_text(); assert "**Status:** wontfix" in original and "split into 31, 32" in original; print("PASS: two milestone-1 parts, ordered blockers, original split; no code verification claimed")'` → passed. `jj log -r broad-editing --no-graph -n 1` → current integration tip still `vkknmrvk` / `27911300`. Scratch-only split; no build, test, or formatting pass is claimed.
Holes filed: none
