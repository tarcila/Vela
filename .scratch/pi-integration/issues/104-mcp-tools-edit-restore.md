# 104 — MCP tools: edit batches and restoration by target_batch_ref

**Status:** resolved
**Milestone:** 1
**Blocked by:** 07, 09, 103
**Files:** src/vsr/integration/mcp/tools/, src/tests/integration/test_IntegrationMcpTools.cpp, src/vsr/integration/mcp/README.md

Part 2 of ticket 15 (split). Contract: spec MCP bullet of Testing Decisions and §9 carrier mapping.

## Desired behavior

- Edit-batch tool and restoration tool (selector `target_batch_ref`, launch-qualified Batch Reference) built on the ticket-103 scaffold, each returning a qualified `operation_ref`.
- Outcomes carry explicit committed / not-committed, `applied_with_sync_failure`, and `conflict` qualified by operation kind; restoration eligibility distinguishes conflicting, `already_restored` and expired. Map policies from tickets 62, 67, 73 faithfully; file holes for gaps.
- Extend README mapping rows; thin loopback tests for happy path and mapping.

## Acceptance criteria

- [x] Edit and restore tools reachable over loopback with qualified refs
- [x] Commit/no-commit and kind-qualified conflict survive the carrier (tested)

## Comments

### 2026-09-28 iteration — resolved
Added `mcp/tools/EditTools.hpp` (`vsr_edit`, `vsr_restore`) on the 103 scaffold. The adapter parses the wire handle, version and value and `target_batch_ref`, and the headless Edit/Restore services own every rule. The terminal status carries `committed` and `result.batch_ref` / `result.target_batch_ref`. The README gains the 104 tool and outcome-by-kind tables. Two loopback scenarios were added to `test_IntegrationMcpTools.cpp`: happy path with qualified refs, replay, `already_restored`, wrong-launch; and edit/restore `conflict`, `applied_with_sync_failure`, envelopes, `forbidden`. Headless modules are unchanged.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (4860 assertions in 128 test cases)
Holes filed: 116-mcp-edit-result-detail, 117-restore-expired-reason
