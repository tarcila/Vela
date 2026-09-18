# Addendum 09 — vsrMcpProxy local error codes and default limits

**Extends:** addendum 03 (non-frozen envelope codes) and addendum 05 item 2. **Source:** hole 225, raised by ticket 215. **Decided by:** orchestrator, ratifying the implementer's choice.

## Decision
- **Error codes:** when the proxy fails a stdio request locally, it returns a JSON-RPC error whose `data.code` is:
  - `timeout` (frozen) when the connect or response deadline passes;
  - `unavailable` when the endpoint is unreachable;
  - `response_too_large` when the response exceeds the byte cap;
  - the attach's own frozen code, such as `occupied`, when a reattach fails.
- **Endpoint ownership:** `untrusted_endpoint` is only a diagnostic. When the listener is not owned by the proxy's effective UID, or ownership cannot be verified, the proxy sends nothing and exits with status 1.
- **Non-frozen names:** `response_too_large`, `unavailable` and `untrusted_endpoint` are not frozen, the same as `invalid_request` in addendum 03.
- **Default limits:** connect deadline 5 s, response deadline 30 s, response cap 16 MiB. All are configurable.
