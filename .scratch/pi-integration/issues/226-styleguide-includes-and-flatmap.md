# 226 — STYLEGUIDE §3/§15: include order and labels; FlatMap instead of std::map; factor the repeated result store

**Status:** needs-triage
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/mcp/McpServer.hpp, src/vsr/integration/operations/Operations.hpp, src/vsr/integration/descriptor/LaunchDescriptor.hpp, src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/render/RevisionRender.hpp, src/vsr/integration/restore/BatchRestore.hpp, src/vsr/integration/sync/SceneSync.hpp, src/vsr/integration/mcp/Json.hpp

Source: standards axis of the round-1 compliance pass. Filed when milestone 1 closed on 2026-09-28, per the human's decision recorded in `.scratch/pi-integration/pipeline.md`. Not a blocker; for triage.

- §3: `McpServer.hpp` has unsorted, interleaved std includes. Several headers are missing the `// vsr` include-block label.
- §15: `std::map` is used for small, stable-key maps where `FlatMap<K,V>` is the documented choice. Sites: AgentEdit, Json, Operations, RevisionRender, BatchRestore, SceneSync. Either switch to `FlatMap`, or document why `std::map` is needed.
- Duplicated code (judgement call): the pair `m_results` (keyed by session and request) plus `m_localResults` appears in AgentEdit, RevisionRender, BatchRestore and SceneSync. Consider one shared helper.

## Comments

### Mechanical include cleanup completed; design work deferred

User approved only the mechanical portion in this pass. Added library/platform
include-group labels, ordered standard and POSIX includes, and removed two
unused includes identified by clangd. The full CPU build with MCP/networking
and all 68 CTest entries passed; fresh-context review found no defects.

This ticket remains `needs-triage`: the `std::map`/`FlatMap` decisions and shared
result-store proposal are not implemented. They need a separate bounded design
and behavioral review rather than being folded into a formatting change.
