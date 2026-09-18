# 139 — Apply addendum-03 policy fixes: synchronizing during the hook and documented envelope codes

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/operations/Operations.hpp, src/tests/integration/test_IntegrationEdit.cpp, src/tests/integration/test_IntegrationOperations.cpp, src/vsr/integration/mcp/README.md, src/vsr/integration/identity/Identity.hpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 03. The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

While an edit's or restore's normal-synchronization hook executes, a status lookup reports `running` rather than `synchronizing` (hole 61). The README documents codes, but not every addendum-03 decision.

## Desired behavior

- A status lookup made from inside the hook of an already-committed operation reports `synchronizing` with `committed=true`. The test in `test_IntegrationEdit.cpp` asserts the literal `synchronizing`.
- Tests cover every addendum-03 behavior that is not already covered, including control-slot reservation for lifecycle calls, withdrawn permission ending `forbidden`, and `invalid_request` consuming no ID.
- `src/vsr/integration/mcp/README.md` lists `invalid_request`, `invalid_host` and `forbidden_origin` as non-frozen codes with their HTTP statuses, plus the Resynchronize `{ready,state}` result shape.

- Also (hole 176): reword the `refreshMetadata` comment in `src/vsr/integration/identity/Identity.hpp`, which still cites the removed `Object::editableName()`.

## Out of scope

- Changing any frozen code name.
- Transport or tool changes beyond the README.

## Comments

### 2026-09-29 iteration — resolved
OperationEngine::runStep no longer resets a committed record from SYNCHRONIZING to RUNNING when it runs the hook step, so lookups made inside the hook now report `synchronizing` (addendum 03 §5). The edit test asserts the literal. New tests cover: lifecycle calls using the reserved control slot, with identical calls coalescing and excess calls `busy`; withdrawn permission ending `forbidden` against session end ending `cancelled`; `invalid_request` consuming no ID in the engine and for empty or duplicate edit batches; and preparation failure ending terminal `busy`. The README now covers the non-frozen codes, the addendum-03 policies and the Resynchronize `{ready,state}` shape. The Identity.hpp comment no longer cites `editableName()`.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && _out/_cmake/RelWithDebInfo/vsrIntegrationTests "[integration]"` → All tests passed (150 test cases), 3 runs
Holes filed: none
