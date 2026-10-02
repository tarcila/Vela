# 02 — Inspect typed parameter values and object references

**What to build:** Read the live camera, renderer, materials, and reference relationships with typed values rather than names/types alone, without acquiring broader write permissions or downloading arrays.

**Blocked by:** none
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Broad Editing](../spec.md), Implementation Decisions 3 and 9; Testing Decisions 1 and 12.

## Current behavior

Inspection exposes general parameter names/types and values only for the five already editable matte/directional-light fields.

## Acceptance criteria

- [ ] Ordinary Boolean/integer/float/string/vector/matrix parameters have explicit ANARI-typed value representations; legacy editable values remain compatible.
- [ ] Large integers use lossless typed decimal encoding and observed non-finite floats use explicit tags; neither is silently rounded or replaced by zero.
- [ ] Valid/null/dangling object references are distinguished and valid references use opaque handles. Arrays expose only type/shape/count summaries without mapping bulk data or triggering GPU work.
- [ ] Unsupported, truncated, and omitted representations carry explicit reasons. Identity/type/version correctness fields are never truncated; item/page/optional/scratch limits apply.
- [ ] Inspection revisions and cursors cover all newly returned values/reference and summary metadata, including non-editable parameter changes.
- [ ] Reading a new field does not widen the object edit allowlist or permissions.
- [ ] Public headless tests and thin MCP tests cover typed values, references, representation limits, and existing object-only compatibility.

## Out of scope

Node inspection/editing, bulk array export, camera/view editing, asset provenance, and broader parameter writes.

## Comments
