# Broad editing implementation

Task: broad-editing
Status: done (implementation complete; nonblocking triage 73 retained)
Integration bookmark: broad-editing
Integration workspace: sibling `broad-editing`
Code base: milestone-1 (74b61ddb before ticket publication)
Spec: spec.md (read-only during implementation)
Addenda: none

## Authorization and scope

The user invoked implement-spec after the proposed twelve-ticket graph. This
is approval to publish that graph locally and implement it. No PR, remote issue,
push, or publication is authorized. Original workspace-local .pi/mcp.json and
checkmatte.png must remain untouched and excluded from feature commits.

## Task graph

| Ticket | Blocked by | Initial status |
|---|---|---|
| 01 object filters/page limits | none | ready-for-agent |
| 02 typed parameter values | none | ready-for-agent |
| 03 layer/node inspection/identity | 01 | ready-for-agent |
| 04 world transforms/affordances | 03 | ready-for-agent |
| 05 restorable node enablement | 03 | ready-for-agent |
| 06 restorable local position | 05 | ready-for-agent |
| 07 restorable local matrix | 05 | ready-for-agent |
| 08 rotation/scale/grouping | 04, 06, 07 | ready-for-agent |
| 09 world position/ancestry restore | 04, 06 | ready-for-agent |
| 10 mixed batches | 06 | ready-for-agent |
| 11 live chess acceptance | 09 | ready-for-agent |
| 12 viewer/docs/full verification | 02, 08, 10, 11 | ready-for-agent |

Ticket Status lines are authoritative; this table is the initial graph, not a
second mutable status registry. Each editing slice includes its full restore,
activity, permission and MCP path. No result-store/FlatMap cleanup or deferred
camera/capture/script feature belongs in this work.

## Implementer conventions

- Jujutsu only, never raw git. Every ticket uses an isolated sibling workspace
  based on the current integration bookmark; no writer shares a working copy.
- Follow the currently deployed ticket-implementer workflow. The host updated
  it during tickets11/12 to one code-plus-ticket commit and removed the home
  scratch-commit hook; do not recreate retired host tooling. Earlier separated
  code/scratch commits and private recovery refs remain valid history. Track
  ignored local working records explicitly; never edit the specification to
  make a partial result look complete.
- Use the confirmed public headless seam and TDD. Read the TDD skill and its
  referenced testing guidance. A ticket is resolved only after actual tests.
- Keep communication sparse: ticket/spec pointers, exact verification commands,
  deliverable and scratch change IDs, and any blocking holes.
- Before reporting, bring the current integration tip into your workspace if it
  changed. Resolve and reverify; do not silently claim old-base work is current.
- The orchestrator will land completed ticket branches serially and verify
  status/commit scope. Worker workspaces are disposable after their work lands.
- Exploration/handoff/verification notes belong in this workspace-local scratch
  directory, never outside the repository or in the source tree.

## Build guidance

The original dev environment already provides ANARI, helide, CMake, Ninja,
compilers, and optional dependencies. Each new workspace has its own build.
Do not copy another workspace's CMake cache. A lightweight ticket configuration:

`cmake -S . -B _out/_cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DVSR_USE_CUDA=OFF -DVSR_USE_INTEGRATION=ON -DVSR_USE_INTEGRATION_MCP=ON -DVSR_BUILD_APPS=ON -DVSR_BUILD_INTERACTIVE_APPS=OFF -DVSR_BUILD_UI_LIBRARY=OFF -DVSR_USE_LUA=OFF -DVSR_USE_NETWORKING=OFF -DVSR_USE_SDL3=OFF -DVSR_USE_TBB=OFF -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX="$PWD/_out"`

Build with at most four jobs per concurrent worker. Build vsrIntegrationTests
and required MCP proxy targets, run the changed test scenarios, then the full
integration entry. Install freshly changed libraries before test passes where
installed-library lookup can win. Keep inherited ANARI/helide library paths;
never replace them with only the new build directory.

Final integration verification includes interactive apps and networking, as
used in the preceding successful 68-test CPU build, plus disabled feature
configurations. CUDA is not automatically claimed as tested.

## Iterations

- Ticket graph published: 12 tickets; first frontier 01 and 02.
- Reserved new-ticket ranges for first frontier: 01 gets 13–18; 02 gets 19–24.
  Numbers are reservations only; do not create dummy ticket files.
- First workers hit stdout-event limits before committing. Their code-only WIP
  and scratch records were parked independently; bounded continuations resolved
  01 and 02 with no holes. Both were merged in qwzpvrloxzvookurnwzonwkllvzkyppo
  (807cc536). Merger verified 186 integration cases / 7,980 assertions, CTest,
  formatting, and CPU build/install. No status is inferred from aborted work.
- 03 was split before implementation in owwpsxpxmvmkvrtwxpksynymrmtuyntz,
  scratch only. No code or test success was claimed. Replacement chain is
  25 layers/root lifetimes → 26 node navigation/lifetimes → 27 fine versions →
  28 structural/ancestry dependencies. Original 03 is wontfix/split. 04 and 05
  are reblocked on terminal replacement 28. All original requirements remain.
- 25 split before code into 31 direct layer/root inspection/lifetimes and 32
  name-filtered layer inspection. Both are vertical headless/MCP slices; the
  orchestrator corrected the draft split's headless-only boundary before work.
  26 now waits for terminal replacement32. No partial code is claimed done.
- Resolved/landed since the initial wave: 31,32 (layers/root identities and
  filtering),26 (node navigation/lifetimes),27 (fine write versions),28
  (structure/ancestry dependencies),04 (world/SRT observations),05 (atomic
  restorable enablement),06 (local position),07 (full matrix). Split parents
  03 and25 are closed wontfix/split; their requirements are covered by the
  terminal replacement28. Heavy workers hit stdout caps; code-only WIP and
  failure comments were preserved and bounded continuations completed them.
- Current source tip before dispatch bookkeeping: zxzpmvyy /516e4882, real
  06+07 merge. The merger consolidated exact-state setter semantics and
  verified 232 integration cases, 15,040 assertions, CTest36/36 and formatting.
- Completed08/09/10/74 and merged them, preserving grouped final-parent
  conversion and post-write dependency journals. 74 closes the audit gap in
  unbounded stored-name substring scans with authenticated resumable linear
  matching, charged work/scratch, and beyond-preview tests.
- Completed11 portable Fool's Mate through both headless and actual loopback
  MCP: four moves in one batch, exact Restore, other28/sharedobjects/parent/
  names/defaults unchanged. 12 verified the actual imported viewer under
  private authenticated UNIX-only Xvfb, isolated XDG configuration, and CPU
  helide; existing views rendered before/moved/restored. All owned test
  processes were stopped; generated build artifacts were excluded.
- Full CPU/UI/network/Lua/SDL3 build/install:68/68 CTest. Integration-disabled
  configuration:35/35. MCP-disabled:36/36. Positive chess filters:6233 headless
  assertions and2666 MCP assertions. No GPU-quality or CUDA-build claim.
- Final two-axis review: original Standards request-changes, Spec approve.
  Single follow-up75 documented the scene-sized registry/container rationale,
  typed transform issue states, and extracted small field-preparation helpers.
  Both targeted postfix axes approved f6499616; final main-workspace68/68
  CTest and focused15796assertions29cases passed. No panel gate was claimed.
- Implementation graph is closed:18 resolved tickets, split parents03/25
  closed wontfix, no ready-for-agent/ready-for-human/needs-info blockers.
  Nonblocking73 remains needs-triage for explicit structural_version carrier
  naming and4096 preparation cap; its safe implementation is documented/tested.
- Configured clangd checks could not resolve the new workspace's project include
  paths despite a generated compilation database; fallback diagnostics were
  unusable, not source compiler failures. No bogus code fixes or host config
  changes were applied. Compiler/build tests are authoritative; temporary
  compile-database link was removed.
- Acceptance evidence retained locally outside the tracked task subtree in
  `.scratch/broad-editing-logs/acceptance/` (selected ASCII logs and scene/UI
  images only, no private descriptors/proofs/Xauthority/runtime files). Worker
  workspaces are cleaned after their completed commits land; integration
  workspace/bookmark broad-editing remains for testing. Original pi-integration
  user config/screenshot are untouched. No PR, issue publication, or push.

## Post-delivery reliability request

The user requested CUDA verification and strongest unattended reliability
before the camera/capture follow-on. Reliability merge6520efdf leaves the main
build CUDA-enabled. Full parallel68/68 and204 repeat executions pass, actual
GPU98,792assertions execute, Compute Sanitizer memcheck is clean, and the
worker's other device diagnostics/profiling are retained as separate evidence.

ASan/UBSan/float-cast-overflow/leak-ON verification found and fixed the JSON
number cast-order UB; added bounded seeded affine/numeric regressions and fixed
an invalid test optional dereference. The parallel remote orbit mismatch was
reproduced through phased delivery and corrected in the test's owning-thread
snapshot/full-pose settlement, not by changing production following logic or
relaxing tolerance. See ../reliability-verification/SUMMARY.md and the committed
orbit diagnosis for exact proof and remaining limits.

The next camera/capture proposal is a draft under ../camera-capture/. It keeps
interactive view state unchanged, uses qualified capture-owned resources and
truthful device/quality/memory provenance, and leaves persistent camera writes
and server file authority outside the recommended first increment. No new
capture implementation is claimed or approved by this reliability record.
