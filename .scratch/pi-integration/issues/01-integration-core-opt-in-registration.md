# 01 — Add the opt-in headless integration core with registration, activation and safe-point execution

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** CMakeLists.txt, src/vsr/CMakeLists.txt, src/vsr/integration/, src/tests/CMakeLists.txt, src/tests/integration/

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §1 (framework boundary and host integration), the capability table and configuration rules of §4, the hook rule in §10, the lease/renewal configuration rule in §4, and Behavioral acceptance coverage item 1. The spec is authoritative; read those sections before starting.

## Current behavior

VSR has no agent-integration module. There is no way for an application to register scenes, views, operations or capability ceilings for external control, and no host safe-point execution mechanism for integration work. `src/tests/` has no integration tests.

## Desired behavior

A new transport-independent, headless integration library exists under `src/vsr/integration/` behind a CMake option, with no ImGui, Lua, Pi or network dependency. Observable through a new Catch2 suite under `src/tests/integration/` registered with CTest:

- Constructing or linking the integration exposes no control until the host explicitly activates it.
- The host registers scenes and views; each view names exactly one registered scene. Unregistered state is not addressable.
- Per-target read/render/edit/resynchronize grants form the capability ceiling. Omitted grants are denied. A render/edit/resynchronize grant without read on the same target is rejected at activation; nothing implicitly grants read.
- Activation is rejected if a scene exposing edit has no normal-synchronization hook, or a scene exposing resynchronize has no recovery callback. Neither capability requires the other's hook.
- Activation is rejected when the lease or renewal duration is non-positive or non-finite, or when renewal is not shorter than the lease. Nothing is clamped; a configuration error is returned to the host.
- Work submitted from another thread never touches scene state until the host services it at a safe execution point. A blocking headless service-loop helper runs that servicing without a graphical loop. A test proves scene access happens only on the servicing thread, inside a service call.
- The suite builds without Lua or UI enabled.
- New `test_*.cpp` files under `src/tests/integration/` and new sources under `src/vsr/integration/` are built without editing a shared CMake file.
- `ctest -R integration` passes in a normal build. The project still builds with the option off.

## Out of scope

- Sessions, authentication, request IDs, discovery/inspection, edits, rendering and restoration (later tickets).
- Any network listener, MCP code, descriptor file or JSON parsing.
- The standard `Context` convenience adapter and viewer execution hook (ticket 16).
- Changes to `src/vsr/scene/`, `src/vsr/rendering/` or `src/vsr/app/` beyond what registration strictly needs to reference existing types.

## Comments

### 2026-09-28 iteration — resolved
Added `vsr_integration` (src/vsr/integration/Integration.{hpp,cpp}) behind `VSR_USE_INTEGRATION` (default ON so a normal build runs the suite): scene/view registration, per-target grants (omitted = denied), activation validation (action-without-read, missing sync hook / recovery callback, lease/renewal checks, no clamping), thread-safe `submit`, `service()` safe point, blocking `runServiceLoop()`/`requestStop()`. Links only vsr_scene + Threads (no UI/Lua/Pi/network). Library sources and `src/tests/integration/test_*.cpp` are globbed (CONFIGURE_DEPENDS), so adding files needs no shared-CMake edit. Registration closes at activation; view/scene registration after activation is left to later tickets (08/16).
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrIntegrationTests && ctest --test-dir _out/_cmake -C RelWithDebInfo -R integration` → 1/1 passed (100 assertions, 6 cases; 5 repeat runs green); `cmake -DVSR_USE_INTEGRATION=OFF _out/_cmake && cmake --build _out/_cmake --config RelWithDebInfo` → builds, then reconfigured back ON.
Holes filed: none
