# 11 — Save retained PNGs through the opt-in client

**What to build:** Save checkmatte.png from a retained successful render/capture through the trusted proxy in an operator-selected directory, without server filesystem rights or rerendering.

**Blocked by:** 04
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 9; Testing Decisions 9 and End-to-end acceptance.

## Acceptance criteria

- [ ] Operator --save-dir explicitly exposes proxy-local vsr_save_image; without opt-in the tool is absent. Strict ASCII basenames cannot choose paths/directories/commands; checkmatte becomes checkmatte.png.
- [ ] Fetch only retained successful images under current authentication and all-target read, distinguishing render/capture kinds; never recapture on expired history or lost authority.
- [ ] Anchor validated no-follow directory-relative operations. Reject traversal, reserved/control names, unsafe ancestry, existing files/symlinks/nonregular targets and unsupported platforms without repair or overwrite.
- [ ] Bound and validate response/decoded PNG, MIME, signature, dimensions and provenance; write an exclusive private temporary file and publish atomically without replacement. Failure cleans only the owned temporary file.
- [ ] Return a traceable receipt without credentials; repeats report destination conflict. Handle local/upstream tool-name collision and truthful file-write annotations.
- [ ] Public proxy/system tests cover both image kinds, short writes, limits, permission failures, expiry and no-overwrite; server state remains unchanged.

## Out of scope

Server saving, arbitrary paths/bytes, overwrite/delete/append, wire asset transfer, Lua/shell and controller-authority changes.

## Comments
