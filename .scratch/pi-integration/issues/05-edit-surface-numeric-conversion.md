# 05 — Add the v1 editable-value surface and deterministic numeric conversion

**Status:** resolved
**Milestone:** 1
**Blocked by:** 01
**Files:** src/vsr/integration/values/, src/tests/integration/test_IntegrationValues.cpp

Contract: `.scratch/pi-integration/spec.md` — Implementation Decisions §8 (editable values and numeric conversion) and Behavioral acceptance coverage item 5. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

No validation exists for agent-supplied parameter values.

## Desired behavior

A new suite `test_IntegrationValues.cpp` shows the exact edit surface: matte color and opacity, and directional light color, irradiance and direction (azimuth/elevation, degrees, each in [0,360]). For every field and boundary:

- 0.1 and integer-looking inputs are accepted. Booleans, strings, malformed input and non-finite values are rejected.
- Opacity `1.0000000000000002` and irradiance `1e39` are rejected. `1e-46` stores +0 and `-0` stores +0. A negative nonzero value fails the nonnegative domain before underflow.
- Rounding is ties-to-even and does not depend on the ambient floating-point mode.
- The irradiance fixture `1.00000005960464477539062582718061255302767487140869206996285356581211090087890625` stores exactly 1.0f.
- Angles are neither wrapped nor canonicalized, and the direction formula matches VSR's convention for the axis cases.
- Unsupported parameters or subtypes and incompatible types are rejected before any mutation.

## Out of scope

- Applying values to scenes, batching or atomicity (ticket 07).
- JSON transport parsing beyond accepting an already-parsed binary64 value plus a type tag.

## Comments

### 2026-09-28 iteration — resolved
Added header-only `src/vsr/integration/values/EditableValues.hpp`: the five-field v1 edit surface, `prepareEdit()` (validates target/type/value on a const Object, no mutation), binary64 domain+FLT_MAX validation, and a bit-level ties-to-even binary64→binary32 rounding independent of `fesetround`. Header-only because `src/vsr/integration/CMakeLists.txt` globs only top-level `*.cpp`; no shared CMake or top-level integration file touched. New suite `test_IntegrationValues.cpp` covers every field, 0.1/integer inputs, kind/non-finite rejection, 1.0000000000000002, 1e39, FLT_MAX boundary, 1e-46/-0 → +0, negative-nonzero rejection, ties under all rounding modes, the 1+2^-24+2^-80 fixture → 1.0f, closed [0,360] angles without canonicalization, VSR axis cases, and unsupported/incompatible rejection with unchanged `lastParameterChange()`.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests && ctest -C Release -R integration` → 1/1 passed (vsrIntegrationTests "[integration]": 310 assertions in 10 test cases, all passed)
Holes filed: none
