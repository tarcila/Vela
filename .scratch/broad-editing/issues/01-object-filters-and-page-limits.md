# 01 — Filter object inspection and advertise paging limits

**What to build:** Find the relevant live scene objects by literal name, type, subtype, or opaque handle without paging through every unrelated object, and expose the active page limits before a request is submitted.

**Blocked by:** none
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 2 and 9; Testing Decisions 3 and 12.

## Current behavior

Object queries page through the registry without filters. The schema declares a page-size minimum but does not advertise the current maximum of 256. Cursor binding and bounded traversal already exist.

## Acceptance criteria

- [ ] Discovery and query schema/descriptions disclose active default/maximum page sizes and supported query modes; default 64, maximum 256. Oversized page sizes reject before admission.
- [ ] Object inspection accepts exact name, literal case-sensitive name substring, exact handle, ANARI object type, and subtype filters; combined filters intersect.
- [ ] Malformed/unsupported filter combinations reject; no matches complete with zero items; stale exact handles report conflict without redirecting.
- [ ] Cursors bind the full query/filter/page policy and reject substitution or relevant state changes. Identical request replay remains historical, not fresh.
- [ ] Sparse/no-match scans preserve visit/slice/deadline/scratch limits and marked continuation semantics; no complete unbounded snapshot is built.
- [ ] Public headless tests and thin MCP tests demonstrate the feature and preserve existing unfiltered object clients.

## Out of scope

New parameter value encoding, layer/node inspection, any new editing capability, container migrations, and result-store extraction.

## Comments
