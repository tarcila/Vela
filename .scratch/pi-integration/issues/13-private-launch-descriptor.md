# 13 — Add private launch-descriptor publication, validation and cleanup

**Status:** resolved
**Milestone:** 1
**Blocked by:** 02
**Files:** src/vsr/integration/descriptor/, src/tests/integration/test_IntegrationDescriptor.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §2 (private launch descriptor and publication) and Behavioral acceptance coverage item 13 (filesystem and schema parts). The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Launch values exist in memory (ticket 02), but nothing publishes them for a client.

## Desired behavior

A new suite `test_IntegrationDescriptor.cpp`, run against temporary directories, shows:

- A strict schema with the exact v1 fields, at most 4 KiB. Duplicate or unknown fields, wrong types and invalid encodings are rejected.
- Directory validation: owned by the effective user, mode 0700 (0600 for the file), no group/other access, safe ancestry, and no symlink substitution. Unsafe directories are rejected, never repaired.
- Publication uses a no-follow exclusive temporary file, then an atomic no-replace publish as `vsr-mcp-<launch_id>.json`. A pre-existing name fails activation closed.
- Readers validate the opened file.
- Orderly shutdown removes only this instance's file, after an identity check. Concurrent launches stay independent.
- Platforms without validated guarantees report unsupported. Linux/POSIX is required; others may report unsupported.
- The path is printed; the credential never is.

## Out of scope

- The network listener (ticket 14). The descriptor's host and port are inputs here.
- Cleanup daemons.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `vsr::integration::descriptor` in `src/vsr/integration/descriptor/LaunchDescriptor.hpp`. It has a hand-written strict v1 JSON parser: 4 KiB cap, UTF-8/BOM/escape checks, duplicate/unknown/missing fields, exact types, and canonical 43-char base64url. It opens directories one component at a time with O_NOFOLLOW. Ancestors must be owned by root or the euid, and must be sticky if group/other or ACL can write them. The final directory must be euid-owned, exactly 0700, with no named ACL entries. Nothing is repaired; a missing final component is created 0700 only when asked. `openOsRuntimeDirectory` uses `$XDG_RUNTIME_DIR`/vsr, falling back to /run/user/<euid>/vsr, and validates it the same way. Publication writes an O_EXCL|O_NOFOLLOW 0600 temp file, then does renameat2(RENAME_NOREPLACE), falling back to linkat. An existing name returns `destination_exists`. Readers use fstat on the opened fd. Cleanup does an fstatat(NOFOLLOW) check of dev/ino before unlinking. `announce()` prints only the path. Non-Linux returns `unsupported_platform`.
Not in this ticket: wiring publication after listener readiness (tickets 14/16). Header-only per ticket 19; no shared/top-level files edited. No retry to salvage.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && ctest --test-dir _out/_cmake -C RelWithDebInfo -R integration` → 1/1 passed (1076 assertions in 32 test cases)
Holes filed: none
