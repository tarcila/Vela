# 18 — Validate Pi attachment and run the end-to-end acceptance workflow

**Status:** resolved
**Milestone:** 1
**Blocked by:** 143, 144, 145
**Files:** docs/

Contract: `.scratch/pi-integration/spec.md` — §14 technical validation gates 1, 4 and 5, and the Pi bullet of Testing Decisions §"Thin adapter checks and end-to-end acceptance". Rendering acceptance must use a real helide device, and libraries must be installed before testing, per the spec's closing Testing Decisions paragraph. The spec is authoritative; read those sections before starting. Business behavior is tested at the public headless integration API (spec §Testing Decisions, "Confirmed primary seam"), with deterministic clients and a controlled monotonic clock.

## Current behavior

No client has attached to a running VSR application.

## Desired behavior

- Using Pi's installed MCP gateway, prove hidden bootstrap and proof handling, reconnect, and renewal independent of model thinking. Only if a gap is demonstrated, add a minimal trusted adapter and record why.
- Start the headless host and then vsrViewer before Pi. Complete inspect → image → edit → fresh render → restore → detach on the procedural fixture.
- Verify second-client denial, abandonment and expiry, and that no secret appears in model-visible output.
- Record the model and image prerequisites and the results in a short doc under `docs/`, including the attachment procedure users follow.
- If the gateway cannot be exercised in this environment, set `Status: needs-info` and explain.

## Out of scope

- Weakening any approved guarantee. Surface feasibility gaps as a hole report instead.
- Aesthetic judgment of the edited image.

## Comments

### 2026-09-28 orchestrator — blocker re-pointed
Ticket 15 was split (`wontfix`, "split into 103, 104, 105"); a wontfix ticket never becomes `resolved`, so this ticket's edge on 15 could never clear. Re-pointed `Blocked by` from 15 to its split successors 103, 104, 105 (all resolved). Also note open ticket 118: inspection/edit results do not yet expose scene/view revision tokens, so an agent cannot form a first render request unaided; and ticket 110: vsrViewer registers no views. Both bear on the end-to-end workflow here.

### 2026-09-28 iteration — ready-for-human
I validated the real Pi gateway (pi-mcp-adapter 3.1.0) against `vsrViewer --agentIntegration`, running build-tree binaries with an explicit LD_LIBRARY_PATH. The transport interoperates: the gateway initializes on 2025-06-18, handles JSON-only replies and a 405 on GET, and a `headers` `!command` supplies launch auth from the descriptor. It cannot run the workflow. `tools/list` is empty because the viewer wires none of the tool families (135). The gateway cannot attach, hold the proof or renew on its own, so a trusted adapter is proven necessary, and its shape needs a decision (136). Ticket 118 (revision tokens) and ticket 110 (no views) also still block. I recorded the prerequisites, the partial procedure and the observed results in `docs/pi-attachment.md`, and faked no step. src/apps/tutorial/ was not touched: the tutorial host has no MCP listener, so it could not serve this workflow.
Verified: `curl` initialize/GET/notifications/vsr/attach/tools/call against a live descriptor → 200/405/202/session issued/invalid_session_proof. `pi -p --no-session --mcp-config <file> …` → server connects, "Server vsr has no tools".
Holes filed: 135-viewer-registers-no-mcp-tools, 136-pi-gateway-needs-trusted-adapter

### 2026-09-28 orchestrator
unblocked by addendum 05 (and 04). Holes 135/136/118 were resolved into follow-up tickets 141 (wire tokens), 143 (viewer tools/view/hooks), 144 (vsrMcpProxy stdio adapter; human-chosen shape) and 145 (headless host MCP + view); Blocked by re-pointed to 143, 144, 145. Run the end-to-end workflow through vsrMcpProxy.

### 2026-09-29 iteration — resolved
Ran the real Pi CLI (`pi -p --mcp-config`, Sonnet 5, `--no-builtin-tools`) through `vsrMcpProxy`, with the app started first. Headless host: inspect → image → edit (sphere to golden yellow; the model saw the change) → fresh render → restore → detach all passed. vsrViewer: the same Pi workflow passed mechanically, but every image was black. With `--agentIntegration` the viewer loads no geometry (filed 201). Visual parity plus the viewer-side lease checks moved to 202 (Milestone 1). Lease matrix on the headless host, driven through the proxy over a FIFO (`.scratch/pi-integration/t18-lease.sh`): a second proxy got `attach failed`/`occupied`. An idle 150 s run kept the session through the proxy's renewal timer. After SIGKILL, an attach 2 s later was denied and one 127 s later succeeded. A stdio `vsr/attach` was refused as `method not found`. The launch credential, `Bearer`, `secret` and `proof` all got 0 hits in both Pi session JSONLs and stdout/stderr; only the public session_id appears, in operation_ref. docs/pi-attachment.md was rewritten with the procedure, prerequisites and results table. No started process remains: host, viewer, Xvfb, proxies and script were killed.
Verified: `cmake --build _out/_cmake --config Release --target vsrMcpProxy vsrTutorialIntegrationHeadless vsrViewer` → ok. Two `pi -p … --mcp-config /tmp/t18-mcp-{headless,viewer}.json` runs → rc=0, each with a 5-step table. `/tmp/t18-lease.sh` log as above. `grep -c <launch_credential>` on the session JSONL/out/err → 0.
Holes filed: 201-viewer-agent-flag-parsed-as-state-file; split remainder: 202-viewer-pi-parity-rerun
