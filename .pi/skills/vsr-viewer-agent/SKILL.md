---
name: vsr-viewer-agent
description: Start (or restart) this workspace's vsrViewer with --agentIntegration and reconnect the vsr MCP server to it. Use when the vsr MCP tools are unreachable, after a rebuild, or when asked to launch the viewer for agent work.
---

Launches the viewer detached from Pi, so it outlives the tool call, and records its descriptor for `mcp-proxy.sh`, the `vsr` server's fixed MCP command. The script is the single source of truth for paths, flags and readiness; run it, never re-derive its steps by hand.

## Steps

1. **Pick flags.**
   - `--build` when sources changed since the last install (runs `just brd`; the rig loads `_out/lib`, so an uninstalled build is stale).
   - `--replace` when a viewer from this workspace is already running. It terminates that viewer and its unsaved scene edits; confirm with the user first unless they asked for a restart.
   - Scene: arguments after `--` replace the default chess scene (`-hdri footprint_court.hdr -gltf ABeautifulGame.gltf`). `-v -e` and `--agentIntegration` are always added.
2. **Run** `.pi/skills/vsr-viewer-agent/start-viewer.sh [--build] [--replace] [-- ARGS...]` from the workspace root, with a timeout of at least 900 s when building. Done when it prints `PID=`, `LOG=`, `DESCRIPTOR=` and `PI=`; on a non-zero exit, report its message and the `[vsrViewer]` log lines and stop.
3. **Hand over the reconnect.** The running `vsr` proxy still targets the previous descriptor (calls fail with `VSR reattach failed`) and only the user can restart it. Ask for `/mcp reconnect vsr` when the script printed `PI=reconnect`, or `/reload` when it printed `PI=reload` (it had to rewrite `.pi/mcp.json`, e.g. on first use). Done when `vsr_discover` succeeds afterwards.

## Gotchas

- The viewer must share Pi's network and `/run/user/$UID` namespaces. A viewer started from a `stockade shell` lives in its own container: its descriptor is invisible here and its loopback unreachable. This script, run from Pi's own `bash`, always launches in the right one.
- The descriptor carries the launch credential: pass its path, never its contents.
- Agent edits live only in the running viewer; a restart reloads the scene files.
- The current descriptor path lives in `.scratch/vsr-mcp-descriptor`; `mcp-proxy.sh` reads it at each (re)connect.
- Output: `.scratch/vsrViewer.log` (line-buffered; grep `[vsrViewer]` for integration status).
