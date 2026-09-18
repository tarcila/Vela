# 136 — Pi gateway cannot bootstrap or renew a VSR session; minimal adapter needed

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** docs/pi-attachment.md

**Kind:** extends

Validation gate 1 (spec §14) was run on 2026-09-28 under ticket 18 against `pi-mcp-adapter` 3.1.0 (SDK `@modelcontextprotocol/client` 2.0.0). Results:

- Transport interoperates. The SDK accepts protocol `2025-06-18`, JSON-only replies and a 405 on GET. Launch auth works through a `headers` `!command` that reads the descriptor.
- The adapter cannot issue the private `vsr/attach` method (ticket 98), and has no private place to hold the returned secret.
- A trusted `requestHeadersCommand` runs for every request with the request body. It *could* attach lazily, cache the secret in a 0600 file, and emit `VSR-Session-ID` / `VSR-Session-Proof`. That would keep the secret away from the model.
- Nothing in the adapter renews on a timer independent of model activity. The lease is 120 s, so an idle Pi session expires. Nothing detaches on exit either. Spec §3 requires renewal "independent of model thinking", supplied by "the gateway, or an adapter if proven necessary".

The gap is therefore demonstrated: the gateway alone cannot satisfy renewal. The adapter's shape is load-bearing and needs a human decision. Options:

1. **stdio proxy (recommended):** a small trusted process Pi launches as a stdio MCP server. It reads the descriptor, attaches, holds the secret in memory, forwards JSON-RPC over loopback HTTP with the proof headers, renews on a timer, and detaches on EOF. It is independent of the Pi adapter's internals, and reconnect is natural.
2. `requestHeadersCommand` helper plus a separate renewal daemon. This keeps the stock gateway path but splits state across two processes and a file.
3. A Pi extension. It couples the code to Pi internals.

Also decide where the adapter's code lives (the repo `tools/`, or `src/vsr/integration/mcp/`), and in which language.

### 2026-09-28 orchestrator
Resolved by addendum 05 (`.scratch/pi-integration/addenda/`). Follow-up ticket(s): 144.
