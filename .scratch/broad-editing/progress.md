# Broad editing implementation

Task: broad-editing
Status: implementing
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
- Follow ticket-implementer's scratch preflight and deliverable/scratch commit
  separation. Track all ignored local working records explicitly. Do not edit
  the specification or sibling tickets to make a partial result look complete.
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
- 31 is the next frontier. Reserved new-ticket range:37–42. Normal-core
  ABA/change-back safety and bounded work remain non-negotiable; no snapshot,
  raw-pointer/slot or blanket layer-token fallback is authorized.
