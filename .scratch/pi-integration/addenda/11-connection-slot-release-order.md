# Addendum 11 — A connection slot is free once its client can observe the close

**Extends:** spec §5 (bounded control/metadata work, busy rejection) and addendum 03. **Source:** hole 222, raised by ticket 221. **Decided by:** orchestrator.

## Decision
The MCP server marks a connection's slot free no later than the moment its socket is closed. Setting `done` (or the equivalent bookkeeping) and closing the fd happen together, under the connection lock the accept loop uses to reap connections. A client that has seen EOF or a completed `Connection: close` response and then reconnects gets a free slot. It never gets `503` merely because the old connection's bookkeeping lags behind the close.

## Why
A client that reconnects straight after a reply sees capacity it is entitled to. Otherwise `maxConnections` rejects well-behaved sequential clients at random, and tests of the capacity limit become flaky.
