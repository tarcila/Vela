# Ticket03 transfer implementation and cumulative qualification handoff

Date: 2026-10-04. Workspace: `/home/tarcila/Code/ANARI/vela/broad-editing`.
Starting change: `vxxspsoo`, parent `yqppllly`. Accepted01/02 source was reused, not restarted.

## Authoritative disposition

**03 is ready-for-human, not resolved.** Public transfer behavior and the cumulative focused matrix pass. Full CTest fails reproducibly in the unchanged SciVis Studio Dataset lifetime case;15 records the load-bearing excluded-scope conflict and memory evidence. Required independent correctness/Standards review is also pending on the already demonstrated outer-orchestrator route from resolved05. Author inspection, analyzer and Memcheck are not relabeled independent review. No complete dependency prerequisite, geometric box, capture, GPU Device or GUI interoperability qualification is claimed.

The whole60-story map below records positive evidence and explicit remaining gates, not a substitute for those gates. No source split, callback/history weakening, image-policy change or manual-signal workaround was introduced.

## Implementation and deliberate reds

- Effective `Forest::move_subtree` calls the existing optional membership-change callback **after** successful relinking. All established root/cycle/self/same-parent/null controls remain no-ops. Moving between Forests is rejected: reparent preserves storage, it is not an invented cross-Layer transfer API.
- Same-Scene cross-Layer copying already calls instrumented `insert_last_child`; accepted01's binding seam binds each clone to its receiving Layer, and accepted02's insertion invalidates destination dependencies. The new tests demonstrate that behavior. No duplicate copy membership notification was added.
- An optional/default-neutral `CopyValidation` preflights source payloads before **any** destination insertion. Layer compares Scene owner-control identities, not numeric indices or reusable raw addresses, and rejects foreign Scene ownership. Validation completes once before recursive cloning, so a foreign descendant cannot leave a partially inserted subtree. Generic Forest values have no validator by default and retain cross-Forest copying. Misaddressed receiving Forests reject copying before touching storage.
- Scene/Layer ownership, payload assignment/move/storage relocation, precise histories, O(1) coherent SceneIdentity revision getters and existing callback/batching machinery are reused unchanged. Copy mutation may walk its actual source subtree for preflight; revision observation does not acquire a walk/fingerprint/history. One fixed optional callback per Forest is bounded bookkeeping, not query history.

Each behavioral red used the existing build tree and a fresh install first:

1. `cmake --build _out/_cmake --target vsrIntegrationTests -j 6 && cmake --install _out/_cmake`, then `_out/_cmake/vsrIntegrationTests '[transfer-validity]'`: exit16, **2 cases/456 assertions,16 failed** (`logs/03-red-reparent*.txt`). Real reparent/change-back already advanced topology history but left both whole-Scene revisions current. Adding the successful membership hook makes the same cases pass456/456,2 cases (`03-green-reparent*.txt`). No explicit signal was added.
2. Leaf/nonleaf, within-Layer and same-Scene cross-Layer cloning was already green on accepted insertion/binding source: **3 cases/952 assertions** (`03-clone*.txt`). It was not falsely attributed to a missing direct copy hook or claimed as a new red repair.
3. Build/install then `_out/_cmake/vsrIntegrationTests 'Scenario: subtree copying rejects foreign Scene ownership before insertion'`: exit44, **1 case/208 assertions,44 failed** (`03-red-foreign*.txt`). Foreign Scene source copying was accepted, including descendants/instance-index cases. Optional preflight makes rejection atomic and the expanded transfer matrix green (`03-green-foreign*.txt`).

Test-development type/FlatMap compile errors are not counted as behavioral reds. The original payload and membership reds remain documented in accepted01/02 records, unchanged. New successful-then-failed-sync sections independently exercise the original six public missing-invalidation families and instance parameters without manual signals.

## Public evidence keys

All primary tests use public real Scene/Layer/LayerNodeData/Forest operations and SceneIdentity observations, not a private validity epoch.

| Key | Actual case and file |
| --- | --- |
| P1 | `attached payload writes invalidate both observations without signals` — `test_IntegrationIdentity.cpp` |
| P2 | `detached work and storage reserve are not attached edits` — same file |
| P3 | `ordinary chosen gap and reused storage bind safe payloads` — same file |
| P4 | `storage relocation preserves exact payload and ancestor history` — same file |
| P5 | `retained Layers and detached payloads qualify both owner lifetimes` — same file |
| P6 | `attached assignment keeps the receiving Layer and exact reference counts` — same file |
| P7 | `attached assignment never reinterprets foreign object indices` — same file |
| P8 | `genuine move-out is detached but the changed source stays attached` — same file |
| P9 | `existing object array active Layer and filter dependencies remain valid` — same file |
| P10 | `automatic dependencies preserve explicit signals and distinct batches` — same file |
| M1 | `public insertion attaches actual members and invalidates observations` — `test_IntegrationMembership.cpp` |
| M2 | `rejected insertion and storage allocation are not membership edits` — same file |
| M3 | `gap filling and erased slot reuse get fresh membership identity` — same file |
| M4 | `public destruction invalidates actual membership and preserves survivors` — same file |
| M5 | `empty destruction and root restrictions keep their established signals` — same file |
| M6 | `removed retained Layers resolve objects but cannot rejoin dependency tracking` — same file |
| M7 | `gaps failure reuse and root clear survive actual Scene teardown` — same file |
| M8 | `mixed membership batches preserve exact structural callback delivery` — same file |
| M9 | `Scene removal with referenced object deletion preserves surviving members` — same file |
| M10 | `membership changed away and back cannot revive an old observation` — same file |
| M11 | `membership append relocation keeps existing identities and exact ownership` — same file |
| T1 | `effective reparent invalidates observations without coarse signals` — `test_IntegrationTransfer.cpp` |
| T2 | `reparent rejected and established no-op controls remain stable` — same file |
| T3 | `public clones bind to their destination and preserve exact payloads` — same file |
| T4 | `subtree copying rejects foreign Scene ownership before insertion` — same file |
| T5 | `transfer controls do not mutate foreign Forest storage` — same file |
| T6 | `clone removal reuse and relocation preserve surviving identities` — same file |
| T7 | `copied payloads qualify source and destination owner destruction` — same file |
| T8 | `transfer edits preserve distinct callback batching contracts` — same file |
| S1 | `automatic validity survives successful then failed synchronization` — `test_IntegrationSync.cpp` |
| G1 | `Forest copy validation is optional and precedes all clone edits` — `test_Forest.cpp` |

Names above are Catch Scenario names without their display prefix. All integration files are under `src/tests/integration/`; core tests are under `src/tests/`.

## Every numbered story: evidence and controls

| Story | Positive public regression / negative control / unchanged supporting case |
| --- | --- |
| 1 local transform | P1 local matrix; S1 original local reproduction after successful sync |
| 2 ancestor transform | P1 ancestor matrix/change-back; S1 original ancestor reproduction |
| 3 transform change-back | P1 ancestor matrix changed away/back without reads; stable public polling |
| 4 enablement | P1 local enabled flag; T3 clone enablement preserved and independently editable |
| 5 disabled ancestor | P1 ancestor enabled flag; S1 original ancestor-enablement reproduction |
| 6 object membership | P1 object pointer/type-index writes; P7 foreign-index negative; T3 exact cloned object counts |
| 7 transform-array membership | P1 transform array; T3 cloned instances resolve same Array with exactly2 then1 uses |
| 8 raw and empty setters | P1 raw scalar/reference/empty/null sections; P2 detached negative |
| 9 defaults/exact transform state | P1 restoration/SRT/exact-state; P4 exact matrix/cache/default; T3 cloned signed/degenerate SRT and restored identity default |
| 10 instance set/replace/clear | P1 separate instance sections; S1 instance reproduction after successful sync |
| 11 instance precise history unchanged | P1 explicitly preserves edit/transform versions for instance writes; T3 copy preserves instance9 |
| 12 Scene empty insertion | M1 named/unnamed empty helpers; S1 original empty-helper reproduction |
| 13 all supported helpers | M1 transform/default/named/array/typed-indexed/new-object forms, independently expected counts and signal counts |
| 14 direct first/last insertion | M1 Forest and ForestNode first/last; S1 original direct insertion reproduction |
| 15 chosen-slot success | M1 next/high chosen slots; M3 filling gaps with independent expected ordering |
| 16 chosen-slot failure | M2 occupied/root/invalid chosen slots: unchanged membership, revisions, counts/history |
| 17 gaps are nonmembers | M2/M3/M7; generic chosen-index Forest test; no binding/membership callback for gaps |
| 18 reuse fresh membership | M3 ordinary/chosen reuse; T6 cloned slot2 reused with fresh lifetime/handle and retired old edit token |
| 19 leaf/nonleaf removal | M4 all public destructive forms; M9 reference deletion; T3 source subtree removal does not corrupt copy |
| 20 direct erase | M4 Forest/ForestNode erase/self; S1 original Layer-erase reproduction |
| 21 subtree/Layer clear | M4/M5/M7 independently expected surviving root/descendant counts and empty-operation signals |
| 22 roots/no-ops | M5 root restrictions; T2 root/cycle/self/same-parent/null; T5 copy root/null/wrong-parent controls |
| 23 removed retained Layer | M6 removal by name/pointer/all and recreate; T7 retained destination teardown |
| 24 effective reparent | T1 both getter orders, identity/topology/ancestry/count/callback invariants; S1 after successful then failed sync |
| 25 reparent away/back | T1 no intermediate read, topology changes and old ordered dependency observation mismatches; S1 |
| 26 subtree copy invalidation | T3 leaf/nonleaf within/cross Layer; T6 self-copy retains existing parent semantics; T4 atomic rejection |
| 27 destination ownership | T3 destroys source subtree/Layer before destination default edit; T7 actual owner destruction; T4 foreign Scene rejection |
| 28 attached copy assignment | P6 same-Scene cross-Layer/detached/source destruction; P7 foreign payload/instance indices reject |
| 29 attached move assignment | P6 receiving attachment/source validity/exact counts; P7 rejected move; P8 transfer back into live source |
| 30 genuine move versus relocation | P8 live move-out stales source; P4/M11/T6 reserve remains observationally stable with surviving attachment/history |
| 31 self-assignment precision | P6 copy/move self-assignment retains established precise-history/count semantics |
| 32 detached construction/copy | P2 stable revisions and balanced counts; T7 detached copy survives owners without keeping them alive |
| 33 retained Layer after Scene dies | P5 object-containing root/local edits; T7 retained copied payload and local clone after actual Scene teardown |
| 34 detached after Layer dies | P5; T7 actual source then destination Layer destruction, detached layer()==nullptr and safe live-Scene resolution |
| 35 balanced reference after Layer dies | P5 and T7 exact live counts3→2→1→0 as nodes/owners/detached payload die |
| 36 delayed cleanup after Scene dies | P5/M7/T7 object resolution absent, local edits safe, replacement Scene cannot revive indices; Memcheck cumulative |
| 37 safe root initialization | P2/P3/M2/M7 root ownership/count/no-edit controls; generic binding runs exactly once for root |
| 38 temporaries/failure/reuse counts | P2/P3/M2/M3/M7/M11 exact counts; G1 preflight rejects before any binding/insertion even after earlier valid sibling |
| 39 relocation lifetimes/history | P4/M11/T6 append/reserve, exact payloads, current edit tokens, node handles/lifetimes and ordered dependencies |
| 40 stale/removed/foreign rejection | P7/T4/T5/T6; unchanged `Handles never redirect to another object`, `incremental node dependencies are bound to live domains and chains` |
| 41 coherent getter orders | P1/M1–M11/T1–T4/T6/T8/S1 generate both orders; second registrations checked by accepted cases and T1 |
| 42 stable polling | P2/M2/T1–T6/T8 observation checks repeat getters3 times without writes; S1 after failed readiness |
| 43 independent Scenes | P1/M1/M4/T1/T3 independent observations remain current; second registrations still stale on own Scene |
| 44 bounded comparisons | Public repeated observations before/after8192-slot reserve in P4/T6; unchanged O(1) dependency folding, no new getter path or fingerprint |
| 45 fixed bookkeeping | Same observations/long relocation tests; structural inspection: fixed callback/owner state and checked revision, no retained per-query mutation history |
| 46 explicit notifications | P10/M5/T8 existing explicit signals still effective; raw automatic cases do not use manual signals |
| 47 no extra coarse callbacks | P1/M1/M4/T1/T3/T8 exact counting delegates; copy uses existing insertion event, not a new render/UI notification |
| 48 structural batching | P10/M8/T8 preserve helper/explicit coalescing separately from raw transfer validity |
| 49 update batching | P10/M8/T8 nested begin/end signals; M8 actual public RenderIndex work4-versus1 coalescing preserved |
| 50 precise/ancestor history | P4/P6/M4/T1/T6; unchanged `Grouped SRT publishes final parent child dependencies and restores`, `World ancestry change-back conflicts but local restore stays narrow`, `World origin conversion uses final ancestors and restores exactly`, `Fools Mate discovers moves verifies and restores one live scene` |
| 51 generic Forest | G1 optional validation/instrumentation versus plain cross-Forest copy; unchanged interface/reparent/chosen-index Forest tests |
| 52 Data Tree | Unchanged `vsr::core::DataTree change notification`, `vsr::core::DataTree update batches`, `A DataNode serializes as the tree it roots`, Text Encoding and DataPath suites; all included in52-case supporting run |
| 53 existing dependency classes | P9 real parameters/references/array-unmap/active-Layer/filter; unchanged `Inspection revisions change independently of edit and render` covers metadata |
| 54 CPU validity versus readiness | S1 successful normal hook then9 public automatic mutation sections and failed recovery; both revisions stale/stable despite view not ready, unrelated view ready |
| 55 historical replay | S1 retained successful edit result replays byte-identically with original SceneRevision after mutation/failure; unchanged `node cursor policy replay and stale scopes never redirect`, `filtered layer cursors bind every filter and replay historical pages` |
| 56 original missing mutations | S1 original six sections plus instance changes have no manual signal; P1/M1/M4 accepted reds and T1 new reparent red are retained |
| 57 independent memory evidence | Cumulative public matrix:30 cases/8894 assertions under Memcheck,0 errors; excluded Studio case separately detects36 errors. Independent human/agent correctness/Standards review still pending |
| 58 rejected/no-op/unrelated controls | P2/M2/M5/T2/T4/T5 and independent Scene tests prevent global/always-changing/signal-only validity from passing |
| 59 honest whole-contract gate | Unchanged `scene bounds never infer a value from missing binding` remains green; this report/03 explicitly withhold completion because full CTest and independent review gates are not green |
| 60 isolation from capture/policy | Unchanged `effective view observation is admitted and capture stays closed` and bounds missing-binding tests remain green; diff is6 scoped source/test files, no encoder/caps/camera/capture/config/spec changes |

Authority/payload suppression remains covered by the unchanged full integration suite, including `bounds publication rechecks current read authority`, session/permission tests, admitted-operation guards and historical-page cases. No public MCP adapter changed, so no new adapter-only claim is made.

## Fresh final verification

All shell calls had the required workspace `cd` prefix. Exact command bodies below omit only bounded stdout summaries and redirects to ignored `logs/03-*.txt`. Existing `_out/_cmake` and `_out` were reused, with CUDA/UI/network/Lua/SDL3 enabled throughout. No cold configuration, sanitizer tree, disabled CUDA or unrelated workspace was introduced.

| Exact command | Actual result / log |
| --- | --- |
| `cmake --build _out/_cmake -j 6 && cmake --install _out/_cmake` | exit0; full enabled build and fresh install before final runtime commands; `03-final-build.txt`, `03-final-install.txt` |
| `_out/_cmake/vsrIntegrationTests '[transfer-validity]'` | exit0, **9 cases/2,025 passed assertions**; `03-final-transfer.txt` |
| `_out/_cmake/vsrIntegrationTests '[transfer-validity],[membership-validity],[payload-validity],[payload-ownership]'` | exit0, **30 cases/8,894 passed assertions**, includes accepted01's10/1201 and02's11/5668 unchanged; `03-final-focused.txt` |
| `_out/_cmake/vsrIntegrationTests '[integration]'` | exit0, **311 cases/118,085 passed assertions**; includes grouped local/world/mixed edits, restore, authority/replay and bounds missing-binding; `03-final-integration.txt` |
| `_out/_cmake/vsrTests '[Scene],[Forest],[DataTree],[DataPath],[ObjectPool],[Object],[Array],[Parameter],[RenderIndex]'` | exit0, **52 cases/1,110 passed assertions**; `03-final-core.txt` |
| `ctest --test-dir _out/_cmake --output-on-failure` | exit8 **twice**, **67/68 passed**,68.64s/68.48s; SciVisStudio80 cases/1501 assertions with1 failure; `03-final-ctest.txt`, `03-repeat-ctest.txt` |
| `valgrind --tool=memcheck --error-exitcode=99 --leak-check=full --show-leak-kinds=possible --errors-for-leak-kinds=definite _out/_cmake/vsrIntegrationTests '[transfer-validity],[membership-validity],[payload-validity],[payload-ownership]'` | exit0, **30 cases/8,894 assertions,0 errors/contexts,0 definite/indirect leaks**, no suppressions; `03-final-valgrind.txt` |
| `clang-tidy -p _out/_cmake src/vsr/scene/Layer.cpp src/tests/test_Forest.cpp src/tests/integration/test_IntegrationTransfer.cpp src/tests/integration/test_IntegrationSync.cpp --checks='-*,clang-analyzer-*'` | exit0,4 translation units; **5 warnings**, all in Sync fixture/AgentEdit paths, including new Sync assertion252 and unchanged shifted276/295/379 plus implicit EditResult outcome construction. Layer/Forest/Transfer units have no emitted diagnostics. This is not a clean-analyzer or independent-review claim; `03-final-clang-analyzer.txt` preserves full traces for review |
| `clang-format --dry-run --Werror src/vsr/core/Forest.hpp src/vsr/scene/Layer.hpp src/vsr/scene/Layer.cpp src/tests/test_Forest.cpp src/tests/integration/test_IntegrationTransfer.cpp src/tests/integration/test_IntegrationSync.cpp` | exit0, no diagnostics; `03-final-format.txt` |
| `sha256sum _out/lib/libanari_library_vsr.so _out/_cmake/libanari_library_vsr.so` | both `a47cc3beb17eeacdd479467328741622d2297b09bc362586bea9cd3df2b5f092` after full build/install |

Scene/core/integration static libraries are relinked into fresh executables; build/install includes changed installable device library. Integration assertion totals may vary with polling in future runs; case counts and these actual results are authoritative. Memcheck is host owner/copy/assignment/move/reuse/gap/failure/relocation evidence, not GPU memory qualification. As in accepted01/02 it reports2,376 possibly lost bytes/15 blocks and4,186,611 reachable bytes/180 blocks in CUDA/TLS initialization, unsupported-driver-ioctl and large-address-range warnings. Those are disclosed, not blanket leak-free evidence. No available configured sanitizer tree was added or claimed.

## Stop, pending gates and resume

Full CTest failed twice and isolated Studio Memcheck found use-after-free;15 contains exact commands/counts and the outside-scope repair decision. No further feature or Studio source work was performed after confirming that load-bearing blocker. Its isolated assertions happen to pass under Memcheck's allocation layout while memory errors remain; neither is erased by the passing focused matrix. Do not conceal this through callbacks/layout changes or weaken the gate.

Independent finish review is pending via the existing outer route, not a new hole; analyzer traces also require honest review/disposition. After15 is properly addressed with separate authorization and fresh positive full CTest/memory evidence, preserve this code, rerun cumulative verification, obtain/dispose independent Standards/correctness review and only then consider resolving03. Downstream complete bounds remains blocked meanwhile. Historical camera/capture tickets, sibling issues04/06–14, specs/addenda, ledgers, executor/configuration, user configuration/screenshots and other workspaces are unchanged.

Budget: scoped executor declares60-minute working budget/4500-second hard deadline, depth1 observed. No actual deadline environment field was exposed. Dispatch stopped for a concrete blocker well before the55-minute time assessment; no superseded20/25-minute split or claimed harness reconfiguration.
