# 02 — Add logical sessions, launch/session authentication and per-target capabilities

**Status:** resolved
**Milestone:** 1
**Blocked by:** 01
**Files:** src/vsr/integration/session/, src/tests/integration/test_IntegrationSession.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §3 (values table, header names, secret encoding), §4 (capabilities and logical-session lifetime), the rejection codes of §9, and Behavioral acceptance coverage items 2 and 3. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Ticket 01's integration core registers targets and capability ceilings, but there are no sessions, credentials, ownership proof or capability checks.

## Desired behavior

A new Catch2 suite `test_IntegrationSession.cpp` shows, at the headless API:

- Each activation generates a fresh cryptographically random 256-bit launch credential (unpadded base64url, 43 characters) and a 128-bit public launch identity (32 lowercase hex). Nothing is reused across activations.
- Attachment with the launch credential creates the only external logical session. It returns a fresh 32-lowercase-hex public session ID and an independent 256-bit session secret (unpadded base64url). A second attach returns `occupied` with no incumbent data. Local host callers consume no slot.
- Session-scoped calls need both the launch credential and current session proof. The distinct codes are `unauthenticated` (missing or invalid launch credential), `invalid_session_proof` (missing, wrong or stale proof) and `forbidden` (valid proof, but the capability is absent or unexposed).
- Effective capabilities are the configured grants narrowed by current host restrictions. They are checked at admission and again at execution. Disable agent edits persists across detach, expiry and a replacement attachment. Only the host can relax restrictions, and only within the ceiling.
- Explicit renewal against a controlled monotonic clock extends the lease from renewal time. Ordinary traffic does not renew. Expiry yields `session_expired`. Detach, expiry and local whole-session revocation permanently invalidate the secret, and a new attachment gets a fresh identity.
- Attachment reports the configured lease and renewal durations.
- No secret appears in logs or in any error text produced by this module.

## Out of scope

- Request IDs, admission, the scene-work pool and cancellation of outstanding work (ticket 03). Here, revocation only needs to mark the session ended.
- Descriptor files (ticket 13) and any HTTP/MCP transport (ticket 14).
- Cursor invalidation beyond exposing a session-ended signal for later tickets.

## Comments

### 2026-09-28 iteration — resolved
Added `vsr::integration::session::SessionAuthority` in `src/vsr/integration/session/SessionAuthority.hpp`. Each activation gets a fresh launch credential (43-character unpadded base64url from `getrandom`) and launch ID (32 lowercase hex). There is one external session slot, with a coarse `occupied` response. Rejections use the frozen codes `unauthenticated` / `invalid_session_proof` / `forbidden` / `session_expired`. Effective capabilities are the configured ceiling minus host restrictions and minus Disable agent edits, and withdrawing read also withdraws the actions that depend on it. Capabilities are checked both at admission (`authorize`) and at the safe point (`authorizeExecution`). Renewal is explicit and uses an injected monotonic clock. Detach, expiry and revocation (`revokeSession`) all invalidate the secret, and a session-ended callback lets later tickets react. Local authority (`authorizeLocal`) uses the ceiling and takes no slot. The module never logs.
Deviations: the session module is header-only because the library glob skips subdirectories and I did not edit shared CMake (see 19). Platforms other than Linux report no secure randomness, so `create()` returns null there. No shared or top-level files were edited.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && ctest --test-dir _out/_cmake -C RelWithDebInfo -R integration` → 1/1 passed (`vsrIntegrationTests "[integration]"`: 329 assertions in 17 test cases)
Holes filed: 19-integration-subdir-sources-not-globbed
