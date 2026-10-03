# Automatic Scene-Bounds Invalidation and Lifetime-Safe Ownership

**Status:** ready-for-agent
**Milestone:** 1

## Problem Statement

A host or agent needs to know whether a scene-bounds observation still describes the live Scene. Relevant public edits can currently leave both SceneRevision and InspectionRevision unchanged. A previously complete box could therefore appear current after the Scene has changed.

This does not mean the renderer as a whole is broken. Existing application workflows often mutate data and then explicitly notify UpdateDelegates. Trustworthy cached bounds need automatic dependency validity even when those notifications are absent, delayed, batched, or suppressed. Adding automatic render/UI notifications to every setter would change those existing workflows and can duplicate work.

The validity mechanism also has to survive actual ownership behavior. A caller can retain a Layer after its Scene is destroyed, or retain a detached object-containing LayerNodeData value after its Layer is destroyed while the Scene remains live. Tracking only one owner's lifetime is insufficient. A copied payload must not carry stale source attachment into a destination node, and ordinary storage relocation must not be treated as an editable move-out.

Earlier short implementation dispatches repeatedly subdivided the work without implementing this coupled foundation. The required observable behavior is already agreed; another list of smaller helper methods must not substitute for implementing and qualifying it.

## Solution

Make relevant public Scene, Layer, LayerNodeData and hierarchy mutations automatically invalidate scene-bounds dependencies through bounded, scene-owned bookkeeping, separate from render/UI notification delivery.

Expose validity through the existing public headless integration revisions. Both scene and inspection observations become stale after relevant edits, regardless of which revision a caller reads first. Repeated reads without changes stay stable. A change followed by a change back does not revive an earlier observation.

Provide lifetime-safe ownership and object resolution across retained Layers, detached payloads, copies, assignments, moves, slot reuse and storage relocation. Preserve existing callbacks, batching, precise edit and ancestor histories, external identities, object-use counts and generic Forest/Data Tree behavior.

This delivers the dependency-validity prerequisite for later complete bounds observations. It does not calculate boxes, implement capture, change image limits, or claim ANARI Device qualification.

## User Stories

1. As a host, I want a local transform edit to invalidate a scene observation, so that I cannot reuse a box computed before the edit.
2. As a host, I want an ancestor transform edit to invalidate a scene observation, so that descendants' changed world placements are accounted for.
3. As a host, I want a transform changed away and back to remain observable, so that an old observation cannot regain validity merely because its final values match.
4. As an agent, I want node enablement changes to invalidate relevant observations, so that effective visibility cannot silently change under cached bounds.
5. As an agent, I want disabled-ancestor changes to invalidate relevant observations, so that subtree visibility remains trustworthy.
6. As a host, I want object-reference membership changes to invalidate scene dependencies, so that boxes cannot describe replaced geometry.
7. As a host, I want transform-array reference changes to invalidate scene dependencies, so that instancing changes are not missed.
8. As a host, I want raw-value and empty-value setters to invalidate dependencies when attached content changes, so that lower-level public APIs do not bypass validity.
9. As a host, I want default restoration and exact transform-state writes to participate in invalidation, so that equivalent editing paths have equivalent safety.
10. As a host, I want instance-parameter set, replacement and clear operations to invalidate dependencies, so that instance state cannot change invisibly.
11. As an application developer, I want instance dependency tracking to preserve existing precise edit and transform histories, so that unrelated restoration contracts do not change.
12. As a host, I want Scene empty-node insertion to invalidate observations, so that newly added hierarchy membership is visible.
13. As a host, I want all supported Scene transform and object insertion helpers to invalidate observations, so that helper overloads have consistent validity.
14. As a host, I want direct first-child and last-child hierarchy insertion to invalidate observations, so that bypassing a Scene helper does not bypass dependency tracking.
15. As a host, I want successful chosen-slot insertion to invalidate observations, so that explicit-index hierarchy construction remains safe.
16. As a host, I want failed chosen-slot insertion to add no membership change, so that rejected work does not pretend to modify the Scene.
17. As a host, I want gap slots created by chosen-slot operations to remain nonmembers, so that allocation layout is not confused with hierarchy content.
18. As a host, I want ordinary and chosen-slot reuse to acquire fresh membership identity, so that old observations cannot revive after removal.
19. As a host, I want leaf and nonleaf removal to invalidate observations, so that deleted contributors cannot remain in a current box.
20. As a host, I want direct hierarchy erase operations to invalidate observations, so that explicit notifications are not a hidden prerequisite.
21. As a host, I want subtree clearing and whole-Layer clearing to invalidate actual dependency changes, so that cached bounds cannot outlive their contributors.
22. As an application developer, I want root restrictions and established no-op behavior preserved, so that tracking does not alter container semantics.
23. As a host, I want Layer removal to invalidate membership observations, so that an externally retained Layer cannot remain implicitly registered.
24. As a host, I want subtree reparenting to invalidate dependencies, so that changed world ancestry is visible without a manual signal.
25. As a host, I want reparenting away and back to remain detectable without an intermediate read, so that historical ancestry is not mistaken for current continuity.
26. As a host, I want subtree copying to invalidate destination dependencies, so that new contributors are included in subsequent observations.
27. As a host, I want supported same-Scene cross-Layer copies to bind to the destination Layer, so that later source destruction does not invalidate their ownership.
28. As a host, I want attached copy assignment to retain destination attachment, so that edits invalidate the Scene containing the destination node.
29. As a host, I want attached move assignment to maintain correct source and destination validity, so that moved values cannot leave an untracked live node.
30. As an application developer, I want moving a value out of a live node distinguished from relocating storage, so that real source edits are detected without inventing edits during reserve.
31. As an application developer, I want self-assignment to preserve existing precise-history behavior, so that tracking introduces no surprising local-version changes.
32. As an application developer, I want detached construction and copying distinguished from attached edits, so that temporary values do not masquerade as Scene membership.
33. As a host, I want a retained Layer to remain safe after Scene destruction, so that access, local edits and destruction do not dereference a dead Scene.
34. As a host, I want a detached payload to remain safe after Layer destruction, so that access and destruction do not dereference a dead Layer.
35. As a host, I want a detached object payload to balance its reference while its Scene remains live, so that destroying its former Layer does not leak an object use count.
36. As a host, I want retained object-containing Layers and payloads to remain safe after Scene destruction, so that delayed cleanup cannot access destroyed object pools.
37. As an application developer, I want root construction to initialize safe ownership before any root-visible operation, so that lifetime tracking cannot depend on a later constructor-body hook.
38. As an application developer, I want insertion temporaries, failed insertions and reused storage to maintain correct counts, so that dependency tracking creates no ownership leak.
39. As an application developer, I want reserve and append relocation to preserve node lifetimes and precise histories, so that storage management does not invalidate external node identity.
40. As a host, I want existing stale, removed and foreign handle rejection preserved, so that new tracking never weakens identity checks.
41. As a host, I want SceneRevision and InspectionRevision to invalidate consistently in either getter order, so that observing one cannot hide a change from the other.
42. As a host, I want repeated revision reads without writes to be stable, so that polling does not invent changes.
43. As a host, I want one Scene's edits not to invalidate another Scene's observations, so that independent hosts remain independently qualified.
44. As a host, I want bounded revision observation independent of node count, so that cache validation cannot unexpectedly walk a large Scene.
45. As a host, I want fixed bookkeeping rather than an ever-growing observation history, so that long-running editing remains predictable.
46. As an application developer, I want existing explicit notifications to remain effective, so that current callers do not need migration to a new notification contract.
47. As an application developer, I want raw setters and direct hierarchy edits not to emit additional render/UI callbacks, so that automatic validity does not duplicate work or introduce reentrancy.
48. As an application developer, I want structural Layer edit batching preserved, so that existing coalesced structural notifications keep their meaning.
49. As an application developer, I want update batching to preserve signal delivery while coalescing delegate work, so that dependency tracking does not conflate the two batch mechanisms.
50. As an application developer, I want precise local edit and ancestor dependency histories preserved, so that grouped edits and conditional restoration retain their guarantees.
51. As a core-library user, I want generic Forest behavior to remain Scene-independent, so that non-scene callers do not acquire a scene dependency.
52. As a Data Tree user, I want hierarchy storage, addressing, Observer behavior and serialization unchanged, so that a scene capability does not alter foundational data semantics.
53. As a host, I want existing geometry/reference/array-write/visibility-filter invalidation coverage to remain correct, so that closing node gaps does not regress other dependencies.
54. As a host, I want validity independent of RenderIndex readiness, so that CPU-side dependency observations remain meaningful after failed synchronization.
55. As an agent, I want retained operation results to keep their original revisions, so that replay is clearly historical rather than silently refreshed.
56. As a maintainer, I want deterministic public-API regressions for each missing mutation class, so that qualification is not based only on explicit-signal controls.
57. As a maintainer, I want independent lifetime and memory-safety evidence, so that token correctness does not conceal dangling ownership.
58. As a maintainer, I want negative controls for rejected/no-op operations and unrelated Scenes, so that an always-changing global clock cannot pass the tests.
59. As a user, I want completion reported only after the entire validity and ownership contract is qualified, so that a partial insertion change is not advertised as complete bounds support.
60. As a user, I want this work isolated from capture implementation and image-policy changes, so that the agreed current limits and future 8K-plus direction remain separate decisions.

## Implementation Decisions

1. **Behavioral scope.** Cover the complete public node payload, hierarchy membership and transfer contract as one coherent capability. The Scene, Layer, LayerNodeData, generic Forest and headless identity modules are the relevant boundaries. Existing object, array, active-Layer and filtering invalidation behavior must remain qualified. No implementation of a bounds cache or geometric scan is required here.

2. **Dependency validity is separate from notification delivery.** Automatic tracking must detect relevant public mutations without relying on callers emitting UpdateDelegate signals. It must not start emitting existing coarse render/UI callbacks from every raw setter, constructor or destructor. Preserve existing explicit callback delivery and helper behavior, including otherwise empty operations that already emit explicit signals. Revision counters need not advance by an exact numerical delta; their external truthfulness and stability are the contract.

3. **Consistent bounded observations.** Maintain owned, scene-local dependency state, or an equivalent mechanism, so revision snapshots and comparisons take O(1) work independent of Layer/node count. No query-time fingerprint, traversal, GPU readback, renderer setup or wait is permitted. Reading either revision must account for changes consistently in both domains; it cannot acknowledge a change for one and leave the other apparently current. Multiple edits may coalesce observationally, but a change-back must not revive an old observation. Repeated reads with no edits remain stable, and independent Scenes remain independent. Counter exhaustion must fail safely rather than reuse old validity.

4. **Actual mutation boundary.** Record successful membership changes and attached public payload changes, not mere allocation or existence of a temporary. Preserve established rejected-operation and no-op semantics. Ordinary insertion, explicit-slot insertion, erase, subtree clear, Layer removal, copy and effective reparent all participate. Direct public hierarchy APIs cannot require a special Scene helper to establish validity. Mutations use the existing owning-thread/safe-point contract; new concurrent mutation support is not introduced.

5. **Both owner lifetimes are qualified.** Lifetime state must distinguish an unavailable Scene from an unavailable Layer. Retained Layers must not resolve or notify a destroyed Scene. Detached payloads must not dereference a destroyed Layer; when their Scene is still live, their object references must retain correct resolution and exactly balanced use counts independently of the former Layer. After Scene destruction, object resolution reports absence safely and cleanup never accesses destroyed pools. Expired ownership must not be revived by allocation reuse. Local retained values can remain readable/editable where their operations do not require an expired owner; this does not keep the Scene alive or re-register a removed Layer.

6. **Ownership starts before root use.** Initialize root-visible ownership machinery before constructing or exposing the root. Specify teardown and revocation order deliberately rather than relying only on member declaration order. Scene-owned shared Layer handles can outlive the Scene, so raw epoch pointers are not qualified simply because a counter is destroyed after the Layer map. Do not add blind constructor/destructor notifications as a shortcut.

7. **Attachment belongs to the receiving node.** Preserve destination attachment for attached assignment and supported same-Scene cross-Layer copying. Newly cloned nodes acquire destination ownership and fresh node lifetimes without retaining a dangling source Layer. Detached values carry only the owner-resolution information their supported operations need, not live-node attachment. Source destruction cannot corrupt a live copied destination. Do not reinterpret object indices across Scenes or invent previously unsupported cross-Scene transfer behavior.

8. **Moves and storage relocation are different operations.** Distinguish an actual move-out that changes a live payload from ObjectPool/Forest relocation that merely moves storage. Qualify construction, copy, move, assignment, root storage, erased-slot reuse, gap slots and failed-insertion temporaries. Preserve existing precise editVersion/transformVersion, ordered ancestry history, node lifetimes, topology histories, values/defaults/SRT and object-use count semantics. Instance-parameter changes must participate in dependency invalidation without silently changing unrelated precise edit/transform history semantics.

9. **Batching remains compatible.** Preserve structural Layer edit batching separately from update batching. The former coalesces its existing structural notifications; the latter still delivers signals as they occur while delegates coalesce work. Automatic validity must not delay dependency detection until an unrelated render/UI signal or introduce new callback counts. Existing atomic editing and conditional restoration contracts remain intact.

10. **Core remains generic.** Any Forest mutation or destination-binding seam must be minimal, optional/default-neutral and independent of Scene concepts. Generic Forest and Data Tree callers retain their public behavior without opting into scene tracking. Data Tree Observer, Data Path and serialization contracts are unchanged. Mechanism selection—including the shape of owned contexts or epoch plumbing—is an implementation-plan decision, not a requirement to reproduce earlier speculative designs.

11. **Integration identity and readiness remain distinct.** Preserve external opaque identities, revision domains, stale/foreign/lifetime rejection and existing authority rules. Internal dependency bookkeeping is not a new public wire identifier. Dependency validity must not imply a synchronized RenderIndex, qualified ANARI Device, completed render, current geometric box or usable capture. Retained historical operations continue to report their original revisions. SceneIdentity's normal owner lifetime contract is not extended to keeping a destroyed Scene alive.

12. **Completion is cumulative.** All mutation, ownership, notification, history and generic-core guarantees must hold together before the capability is declared complete. Intermediate work may be independently tested, but insertion-only or counter-only changes cannot qualify whole-scene bounds dependencies. Preserve requirements when changing the execution plan; do not let the short-ticket workflow turn the inseparable ownership foundation into repeated paper splits or a weaker product contract.

## Testing Decisions

1. **Primary seam: the existing public headless integration with real Scenes.** This is the behavioral seam already confirmed in the earlier discussion. Use public Scene/Layer/LayerNodeData/Forest mutation APIs and observe SceneRevision and InspectionRevision through the existing identity interface. Do not invent a private test-only validity API. Tests assert that old observations are stale and subsequent stable observations stay current, not the representation of an epoch, ownership control block or cache.

2. **One shared mutation matrix.** Extend existing integration identity tests to cover the demonstrated local/ancestor transforms, ancestor enablement, Layer erase, empty-node insertion and direct hierarchy insertion gaps. Extend the same matrix to default/SRT/raw-value/object/transform-array/instance changes, every helper overload, ordinary and chosen-slot insertion, destruction/clear/removal, copy/reparent, reuse and change-back. Use independently expected Scene membership, transform values and identities. Run both getter orders, batched edits and multiple edits without intermediate reads.

3. **Negative controls are mandatory.** Check independent Scenes, stable polling, rejected object lookups, occupied/invalid explicit slots, established no-op reparent behavior, detached construction and storage reserve. Preserve legitimate existing explicit-signal invalidation without asserting a particular internal counter increment. An always-changing clock, global cross-Scene invalidation or a signal-only implementation must fail these tests.

4. **Lifetime behavior uses existing public ownership seams.** Extend Scene and LayerNodeData tests for retained object-containing Layers after Scene destruction; detached object-containing payloads after Layer destruction while Scene remains live; and detached values after Scene destruction. Access and destroy survivors, and assert object-use count balance while the Scene lives. Include removal while retained, copy/assignment followed by source destruction, supported cross-Layer copies, destination edits, self-assignment, actual move-out and storage relocation. Do not dereference a NodeRef after its owning Forest is destroyed; safe survivors are retained Layers and detached values, not promised immortal node references.

5. **Notification and precision regressions are externally observable.** Use counting UpdateDelegates to verify exact existing callback behavior for raw edits, Scene helpers, direct hierarchy operations and mixed batches. Extend existing grouped-edit, restore, node-lifetime, precise local-version and ancestor-history tests without loosening their expectations merely to accommodate a convenient design. Instance dependency tests explicitly distinguish bounds validity from precise transform/edit history.

6. **Generic-core compatibility uses prior art.** Existing Forest and Data Tree tests exercise first/last/chosen-index operations, gaps, failure, root behavior, erase/copy/reparent, reserve/relocation, and serialization/Observer behavior. Run them with the default generic configuration and, where applicable, with optional scene-independent mutation instrumentation. These are supporting compatibility tests, not a second private primary validity seam.

7. **Existing dependency classes and synchronization remain qualified.** Regress real object parameters/references, array write/unmap, active Layers, filtering and metadata behavior. Exercise a Scene changed after a previously successful synchronization and a subsequent failed synchronization: old revisions do not become current, while CPU dependency observations do not require ready rendering. Existing admitted operations, authority suppression and historical replay continue to behave correctly; add thin MCP checks only if a public MCP adapter contract actually changes.

8. **Memory safety is separate evidence.** Run targeted owner-destruction, move/copy/assignment, reuse, gaps, failure and relocation cases under available sanitizers or Valgrind. Verify actual test matches, case counts, assertion counts and tool exits. Record unsupported tools or coverage gaps honestly; a green non-sanitized suite is not sanitizer evidence. Prefer the existing configured build trees over unrelated workspace mutation or cold configuration.

9. **Build and install before runtime verification.** Keep the existing CUDA/UI/network/Lua/SDL3-enabled configuration. Build changed code and install changed libraries before runtime tests so installed-library precedence cannot exercise stale code. Run focused new tests, existing integration/broad-editing regressions and the applicable full CTest suite. Formatting and independent correctness/standards review accompany completion. No GPU ANARI capture or GUI interoperability qualification is inferred from these tests.

10. **Record a proof of completion, not a list of edited modules.** Each mutation and lifetime obligation must map to an actual public regression or a documented unchanged existing test. Preserve deliberate red reproductions and demonstrate they become positive through the new behavior, not by adding manual signals to the tests. Report commands, positive counts, memory-tool findings and remaining limitations. Partial source changes, metadata disposition and task splitting are never counted as implemented behavior.

## Out of Scope

- Computing analytic/triangle bounds, installing complete host boxes, cooperative geometry providers, cache storage/publication, or expanding the missing-binding-only bounds tool.
- Offscreen cameras, viewport binding, capture submission/poll/readback, progressive samples, denoising, PNG saving or CPU/CUDA ANARI capture qualification.
- Changes to the already qualified integration PNG encoder, the current 8 MiB PNG cap, pixel/dimension/transport limits, or implementation of 8K-plus rendering. Current limits remain policy rather than a permanent product ceiling; future larger rendering requires its own coordinated resource/transport work.
- A general render/UI notification overhaul, migration of existing callers to a new signal contract, changes to Data Tree Observer/addressing/serialization semantics, or moving Scene concepts into the core library.
- New concurrent mutation guarantees, support for invalid references after container destruction, arbitrary cross-Scene object-index transfer, or general ownership redesign unrelated to the required live and retained objects.
- Broad container migration, shared result-store refactoring, deferred scratch-guard/schema cleanup, and other unrelated Standards findings.
- Editing the frozen camera/capture spec, rewriting prior ticket history, marking unfinished source work complete, remote issues/PRs/pushes, or changes to user-local configuration and screenshots.

## Further Notes

### Existing state and relationship to camera/capture

This is a focused synthesis of the agreed behavior, not a claim that scene invalidation or ownership implementation already exists. The integration PNG work is completed and independently reviewed; it is not reopened by this spec. No source implementation for the automatic validity foundation is claimed at publication.

The frozen parent contract remains [.scratch/camera-capture/spec.md](../camera-capture/spec.md). Authorization and the original public mutation reproduction are recorded in [ticket 29](../camera-capture/issues/29-scene-bounds-invalidation-scope.md). The execution blockage and ownership findings are recorded in [ticket 42](../camera-capture/issues/42-scene-helper-insertion-validity.md). Historical replacement requirements remain in tickets 34 through 43. [The repair-run ledger](../camera-capture/repair-run.md) and [verification report](../camera-capture/repair-review.md) distinguish completed encoding from unimplemented scene validity.

Publishing this spec does not resolve, renumber, delete or automatically supersede those tickets. A subsequent implementation plan should map their complete requirements into a coherent execution unit, retain historical dispositions and keep complete host-bounds work blocked until cumulative qualification. Product decisions are settled; a longer execution approach is needed for the coupled foundation, rather than another interview or more helper-only subdivisions. This spec publication does not itself change harness deadlines or authorize implementation.

### Vocabulary and architectural boundaries

Use Scene, Layer, LayerNodeData, Forest, UpdateDelegate, SceneRevision, InspectionRevision, RenderIndex, Data Tree, Data Node, Data Tree Observer and Data Path in their existing domain meanings. A scene dependency change is not automatically a Data Tree Signal or a render/UI notification. Respect the existing library dependency layers and the authority/identity boundaries in [ADR 0029](../../docs/adr/0029-mint-all-scene-object-identity-on-the-server.md) and [ADR 0030](../../docs/adr/0030-split-authority-between-round-tripped-structure-and-optimistic-parameters.md); no Studio wire authority or object-identity changes are introduced.

### User confirmation already available

The user approved automatic dependency validity while preserving callback/batching/history behavior, lifetime-qualified ownership, generic Forest independence and an agent-selected implementation mechanism. The existing public headless integration seam with real Scenes was confirmed before this synthesis. There is no outstanding product question requiring another grilling round; implementation design may surface a genuine compatibility tradeoff, which must be reported rather than silently weakening this contract.
