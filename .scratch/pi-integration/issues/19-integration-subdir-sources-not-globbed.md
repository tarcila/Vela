# 19 — vsr_integration does not compile sources in subdirectories

**Status:** resolved
**Milestone:** 1
**Blocked by:**
**Files:** src/vsr/integration/CMakeLists.txt

**Kind:** extends

`src/vsr/integration/CMakeLists.txt` globs only `${CMAKE_CURRENT_LIST_DIR}/*.cpp`, so `.cpp` files under the per-ticket subdirectories (`session/`, `operations/`, `identity/` and the rest) are never compiled. Ticket 02 had no permission to edit that shared file while tickets 04 and 05 ran concurrently. It therefore made `src/vsr/integration/session/SessionAuthority.hpp` header-only, with every function `inline`.

Assumption: header-only is acceptable for now. Revisit by making the glob recursive (`GLOB_RECURSE ... *.cpp`) in one small change. After that, subdirectory modules such as `session/` can move their definitions into `.cpp` files. If the glob stays as it is, every later subdirectory ticket either has to ship header-only code or edit the shared file.

## Comments

### 2026-09-28 orchestrator
Resolved by addendum 02 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 138.
