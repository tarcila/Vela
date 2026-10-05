#!/usr/bin/env bash
# Stable MCP entry point for the `vsr` server: execs this workspace's
# vsrMcpProxy on the descriptor recorded by start-viewer.sh. Because the
# config never changes, `/mcp reconnect vsr` picks up a restarted viewer.
set -euo pipefail
workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
record="$workspace/.scratch/vsr-mcp-descriptor"
[[ -s "$record" ]] || {
  echo "no recorded descriptor; run start-viewer.sh" >&2
  exit 1
}
exec "$workspace/_out/bin/vsrMcpProxy" "$(<"$record")"
