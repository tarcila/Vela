# 227 — Minor robustness notes: HeadlessServiceLoop watcher join on throw; missing <cctype> in Inspection.hpp

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/context/ContextAdapter.hpp, src/vsr/integration/inspection/Inspection.hpp

Source: coordinator notes, post-review:1:1 packet 2 (report 16ca739a). Filed when milestone 1 closed on 2026-09-28, per the human's decision recorded in `.scratch/pi-integration/pipeline.md`. Not a blocker; for triage.

- `HeadlessServiceLoop::run` leaves its watcher thread joinable if `runServiceLoop` throws, which would call `std::terminate`. Make the join exception-safe.
- `Inspection.hpp` uses `std::isxdigit` and `std::isupper` without including `<cctype>`.

## Comments

### Follow-up implementation

User approved this robustness fix after the milestone closed. Added a private
RAII watcher state to `HeadlessServiceLoop`: destruction signals completion
and joins the thread on normal return and exception unwinding, without a
library catch/rethrow block. Added the direct `<cctype>` include to inspection.

Regression coverage uses the existing public headless boundary: submitted
work throws and the host's logging callback throws while reporting it. The
new scenario aborted with SIGABRT before the fix and propagates the expected
exception after it. Existing headless stop scenarios still pass. Full CPU
build with MCP/networking enabled and all 68 CTest entries passed.

Fresh-context correctness review found no defects. The standards review's
class-comment finding was corrected. No new API or test-only hook was added.
