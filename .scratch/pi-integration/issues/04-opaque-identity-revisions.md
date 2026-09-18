# 04 — Add opaque object handles, parameter-version tokens and scene/view/registry revisions

**Status:** resolved
**Milestone:** 1
**Blocked by:** 01
**Files:** src/vsr/integration/identity/, src/tests/integration/test_IntegrationIdentity.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §6 (identity and revision semantics) and Behavioral acceptance coverage item 4. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

Only raw storage indices and existing Object parameter versions exist. There are no external handles or revision tokens with the contract's guarantees.

## Desired behavior

A new suite `test_IntegrationIdentity.cpp`, using real VSR Scenes, shows:

- Opaque handles never redirect to another object within a launch. This holds for removal, index reuse and scene replacement; defragmentation either remaps correctly or explicitly invalidates.
- Handles and tokens from an earlier activation never authorize later-activation edits.
- Object parameter-version tokens change on any parameter change of that object, including change-back-to-original.
- Scene revisions change on render-relevant content changes. View revisions cover camera, renderer, dimensions and Image Pipeline configuration.
- Registry/inspection revisions change on returned metadata, returned parameter values, membership/order, parameter structure, registration or handle mapping, and can change without the edit or render revisions changing.
- Metadata-only label changes are not parameter changes.

## Out of scope

- Changes to archive schemas or persistent UUIDs.
- Paging and cursors (ticket 06), and edit conflict handling (ticket 07). This ticket only provides the tokens and handles those tickets consume.
- Modifying `src/vsr/scene/` beyond minimal, justified hooks needed to observe changes. Note any such hook in a comment.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `vsr::integration::identity` in src/vsr/integration/identity/Identity.hpp. It is header-only because the library CMake globs only top-level `*.cpp`, so no shared CMake file changed. `IdentityAuthority` is per activation and stamps every token with a random nonce. `SceneIdentity` observes one Scene through an update delegate and a defrag callback, and issues never-reused serial handles. Removal, removeAll and scene replacement invalidate those handles; defragmentation remaps them. It also issues per-object parameter-version tokens, a scene revision, and an inspection revision (metadata via `refreshMetadata`, `noteRegistrationChanged`). `ViewRevisionTracker` covers camera and renderer (including their parameter changes), dimensions and `notePipelineChanged`. No `src/vsr/scene/` hook was needed. No prior WIP existed to salvage.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && ctest --test-dir _out/_cmake -C RelWithDebInfo -R integration` → 1/1 passed (190 assertions, 13 cases; 3 repeat runs green).
Holes filed: 25-identity-unsignalled-changes
