# Addendum 05 — vsrViewer integration parity and the Pi stdio adapter

**Extends:** spec §1 (Pi adapter "only by a demonstrated gateway gap"), §3 (trusted client code holds the secret; renewal independent of model thinking), §13, and §14 gates 1 and 5. **Source:** holes 98, 109, 110, 135, 136. **Decided by:** human (adapter shape, 2026-09-28) and orchestrator.

## Decisions
1. **Gap demonstrated (136):** with pi-mcp-adapter 3.1.0, Pi's MCP gateway cannot issue `vsr/attach`, cannot hold the session secret privately, and cannot renew independently of model activity. A minimal trusted adapter is therefore justified.
2. **Adapter shape (human choice):** a C++ **stdio MCP proxy**, `vsrMcpProxy`, lives in `src/apps/tools/` and is built only with `VSR_USE_INTEGRATION_MCP`. Pi launches it as a stdio MCP server. It:
   - is given the descriptor path (argument or environment);
   - validates the opened descriptor with the repo's descriptor code;
   - attaches with the launch credential and holds the session ID and secret only in memory;
   - forwards JSON-RPC messages to the loopback endpoint with the `Authorization`, `VSR-Session-ID` and `VSR-Session-Proof` headers;
   - renews on a timer at the attachment-reported cadence, independently of message traffic;
   - reattaches on reconnect only as a new session (it never revives old authority);
   - detaches on stdin EOF or signal.
   It never exposes the secret or the credential in forwarded results, tool lists, stdout diagnostics or logs. `vsr/*` methods stay private to the proxy. The model sees only the server's tools.
3. **Bootstrap carrier (98):** the non-tool JSON-RPC methods `vsr/attach`, `vsr/renew` and `vsr/detach`, over JSON-only streamable HTTP with protocol version 2025-06-18, are confirmed as the carrier between the proxy and the server.
4. **Viewer parity (110, 135):** with `--agentIntegration`, vsrViewer:
   - registers all four MCP tool families: query, lookup, edit and render/resync;
   - registers at least one renderable view backed by a helide device with its own offscreen Image Pipeline, independent of the interactive viewport. Its fixed camera initially matches the viewport camera.
   - its normal-synchronization hook and recovery callback confirm that the RenderIndex/Image Pipeline state of that view has been updated for the current scene, rather than returning `true` unconditionally.
   - grants read/render/edit/resynchronize on the registered scene and view by default, subject to the existing capability-ceiling rules.
5. **Activity enumeration (109):** `ActivityLog` provides an ordered enumeration of its retained entries, and the viewer model uses it instead of probing IDs.

## Why
Spec §14 gate 5 requires the same inspect/edit/render/restore workflow in a headless host and in vsrViewer, with Pi started separately. That workflow cannot run without tools, a renderable view, private bootstrap and independent renewal.
