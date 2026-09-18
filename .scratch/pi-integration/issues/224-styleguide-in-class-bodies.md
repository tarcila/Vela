# 224 — STYLEGUIDE §5: move non-trivial method bodies out of class bodies in the integration headers

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/activity/Activity.hpp, src/vsr/integration/mcp/Json.hpp, src/vsr/integration/edit/AgentEdit.hpp, src/vsr/integration/mcp/McpServer.hpp, src/vsr/integration/render/RevisionRender.hpp

Source: standards axis of the round-1 compliance pass (code-review skill, 2026-09-28). Filed when milestone 1 closed on 2026-09-28, per the human's decision recorded in `.scratch/pi-integration/pipeline.md`. Not a blocker; for triage.

STYLEGUIDE §5 says method definitions are never written inside the class body, except for a truly empty body. Several new integration headers break this, for example `ActivityLog::append/update/get` and `json::Value` accessors. Move the non-trivial bodies to each file's "Inlined definitions" section. Behavior must not change; the full `[integration]` suite must still pass.

## Comments

### Follow-up implementation

User approved the mechanical layout cleanup after the milestone closed.
Moved nonempty member bodies in all five listed headers to qualified
out-of-class `inline` definitions. Empty constructor bodies remain in class;
the friend Batch Reference equality operator is now defined outside its class.

Verified clang-format, a read-only token comparison of function/control bodies
against the pre-cleanup revision (all identical), the full CPU build with
MCP/networking enabled, and all 68 CTest entries. Fresh-context correctness and
standards reviews found no remaining issues in these moves. APIs, container
choices, and result-store behavior are unchanged.
