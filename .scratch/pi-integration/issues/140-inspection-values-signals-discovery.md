# 140 — Return editable values from inspection, add incremental metadata signals, admit discovery session-wide and fix the removal crash

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/inspection/, src/vsr/integration/identity/, src/vsr/integration/operations/Operations.hpp, src/vsr/scene/Object.h, src/vsr/scene/Object.cpp, src/tests/integration/test_IntegrationInspection.cpp, src/tests/integration/test_IntegrationIdentity.cpp

Contract: `.scratch/pi-integration/spec.md` plus addendum 04 (items 1–5). The spec and the addenda are authoritative; read them first. Business behavior is tested at the public headless integration API, with deterministic clients and a controlled clock.

## Current behavior

Current behavior:
- Inspection items omit parameter values.
- Each inspection slice runs `refreshAllMetadata()`, which is O(objects).
- Discovery is admitted against scene 0.
- Inspecting after removing 149 of 150 fresh materials segfaulted in ticket 06's testing.

## Desired behavior

- Inspection items for matte materials and directional lights carry the current values of the §8 editable parameters, in the serialization addendum 04 specifies.
- Name and metadata changes bump the inspection revision through an incremental signal. No per-slice full-scene refresh remains. Any `vsr::scene::Object` hook is minimal and justified in a ticket comment.
- Discovery is admitted session-wide and works when only a non-first scene is readable. Zero readable targets gives an empty completed page. The retained record's covered targets are exactly the listed targets, and foreign lookups check read on each of them.
- A regression test creates 150 materials, removes 149 and inspects without crashing. The root cause is fixed and described in a ticket comment.
- The full `[integration]` suite passes.

## Out of scope

- Revision tokens on the wire (ticket 141).
- Returning values for non-§8 parameters.

## Comments

### 2026-09-29 iteration — resolved
Inspection items now carry `"values"` for matte color/opacity and directional color/irradiance/direction. Numbers are `%.17g` of the stored float32, and direction is `[az, el]`. Discovery is `Request::sessionWide`: admission and execution check only that the session is live, and it runs on the first registered scene as its carrier. The executor returns the listed targets in `Step::covered`, and those become the record's covered targets. A foreign lookup needs read on each of them; an empty page needs none.
Scene hook: I added an optional virtual `BaseUpdateDelegate::signalObjectMetadataChanged(const Object*)`, defaulting to a no-op, and `MultiUpdateDelegate` fans it out. `Object::setName`, `setMetadataValue/Array` and `removeMetadata` emit it through a private `metadataChanged()`. It is needed because names and metadata had no signal at all, so the only alternative was the O(objects) per-slice refresh, which I removed. This touched `UpdateDelegate.hpp/.cpp`, which are outside the listed Files but are the smallest place for the hook.
Crash root cause: the test held raw `Object*` from `createObject<T>().data()` across later creations. `ObjectPool` stores values in a `std::vector`, so growth reallocates and those pointers dangle. `removeObject(dangling)` then crashed in `SceneIdentity::Observer::signalObjectRemoved` (confirmed with gdb). Identity and inspection were not at fault: removal through fresh lookups, then `removeUnusedObjects` and defragmentation, followed by inspection, is covered by the regression test and is safe. No library change was needed. Callers must not keep `.data()` pointers across creation.
`test_IntegrationMcpTools.cpp` (outside Files; ticket 141 also touches it) had to change one assertion. Its "own receipt without read" case restricted the scene before the discovery ran, so the page listed nothing, and under the new rule the record then covers nothing and its result is visible. The test now restricts after the drain. Ticket 141 should wait on this ticket (140) as it already does.
Verified: `cmake --build _out/_cmake --config Release --target vsrIntegrationTests vsrTests && _out/_cmake/Release/vsrIntegrationTests "[integration]"` → All tests passed (5534 assertions in 141 test cases); `vsrTests` → All tests passed (4967 assertions in 115 test cases)
Holes filed: 152-activity-discovery-covered-targets, 153-editable-name-bypasses-metadata-signal

