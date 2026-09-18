# Addendum 10 — Edit values carry full JSON content for replay canonicalization

**Extends:** spec §9 (canonical request comparison) and addendum 03. **Source:** hole 237, raised by ticket 217. **Decided by:** orchestrator, ratifying the implemented change.

## Decision
Parsed edit values keep the full content of every JSON kind all the way into `EditService::canonicalArguments`, so canonical comparison sees it. The §8 edit surface is unchanged: a non-number value still rejects with `invalid_request` at validation. Reusing a request_id with different non-number content now returns `request_id_conflict`, as §9 requires, instead of silently replaying.
