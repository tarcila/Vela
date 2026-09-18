# 237 — Edit values now carry full non-number JSON content (scope extension of 217)

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/integration/values/EditableValues.hpp, src/vsr/integration/mcp/tools/EditTools.hpp

**Kind:** extends

Ticket 217 required `EditService::canonicalArguments` to encode the full
content of every JSON kind. That content was discarded upstream, before
canonicalization: `mcp::tools::editValue` converted booleans, strings, null
and objects into a kind-only `JsonValue`. Without changing that, no
canonicalization fix could have worked.

Assumption made (commit loqxntvq): `values::JsonValue` gained `boolean`,
`text` and `members` (a vector of key/value pairs sorted by key) fields, and
`editValue` now fills them. Objects share the depth cap that arrays already
had (> 4 → MALFORMED). The edit surface still rejects every non-number kind
by kind, so validation behaviour is unchanged.

Revisit if rejected: the two files lie outside 217's `Files:`. One
alternative is to canonicalize the raw `json::Value` at the MCP tool layer and
pass it through `EditBatch`. That keeps `values::JsonValue` minimal, but it
splits canonicalization between two layers.

### 2026-09-28 orchestrator
Resolved by addendum 10 (ratified as implemented). No follow-up.
