#!/usr/bin/env bash
# Start this workspace's vsrViewer with --agentIntegration, detached, and
# record the descriptor it publishes for mcp-proxy.sh, the `vsr` server's
# fixed MCP command.
#
# Usage: start-viewer.sh [--build] [--replace] [-- VIEWER_ARGS...]
#   --build    run `just brd` first (build + install into _out/)
#   --replace  stop a running vsrViewer from this workspace first
#   VIEWER_ARGS default to the chess scene used during development.
#
# Prints PID=, LOG=, DESCRIPTOR= and PI= lines on success; exits non-zero
# with a reason otherwise. PI=reconnect means `/mcp reconnect vsr` suffices;
# PI=reload means .pi/mcp.json was (re)written and Pi needs `/reload`. Never reads the descriptor's contents (it holds the
# launch credential).
set -euo pipefail

workspace="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
viewer="$workspace/_out/bin/vsrViewer"
proxy="$workspace/_out/bin/vsrMcpProxy"
entry="$workspace/.pi/skills/vsr-viewer-agent/mcp-proxy.sh"
record="$workspace/.scratch/vsr-mcp-descriptor"
log="$workspace/.scratch/vsrViewer.log"
mcp="$workspace/.pi/mcp.json"
timeout_s=120

build=0
replace=0
args=()
while (($#)); do
  case "$1" in
  --build) build=1 ;;
  --replace) replace=1 ;;
  --) shift; args=("$@"); break ;;
  *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
if ((${#args[@]} == 0)); then
  args=(-hdri /data/Envmaps/footprint_court.hdr
    -gltf /data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf)
fi

cd "$workspace"

# Exact executable match only: `pgrep -f` with a looser pattern also matches
# the shell running this script.
running() { pgrep -xf "$viewer .*--agentIntegration" || true; }

if [[ -n "$(running)" ]]; then
  if ((replace)); then
    pkill -TERM -xf "$viewer .*--agentIntegration" || true
    for _ in $(seq 50); do [[ -z "$(running)" ]] && break; sleep 0.2; done
    [[ -z "$(running)" ]] || { echo "vsrViewer did not exit" >&2; exit 1; }
  else
    echo "vsrViewer already running (PID $(running | tr '\n' ' '));" \
      "rerun with --replace" >&2
    exit 1
  fi
fi

if ((build)); then
  just brd
fi
[[ -x "$viewer" ]] || { echo "missing $viewer; rerun with --build" >&2; exit 1; }
[[ -x "$proxy" ]] || { echo "missing $proxy; rerun with --build" >&2; exit 1; }

mkdir -p "$(dirname "$log")"
: >"$log"
setsid nohup stdbuf -oL -eL "$viewer" -v -e "${args[@]}" --agentIntegration \
  >"$log" 2>&1 </dev/null &
pid=$!

descriptor=""
for _ in $(seq $((timeout_s * 5))); do
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "vsrViewer exited early; see $log" >&2
    tail -n 20 "$log" >&2
    exit 1
  fi
  descriptor="$(sed -n 's/.*\[vsrViewer\] MCP descriptor: \(.*\)$/\1/p' "$log" | tail -n1)"
  [[ -n "$descriptor" && -f "$descriptor" ]] && break
  if grep -q "agent view setup failed\|agent integration activation failed\|MCP listener:\|runtime directory:" "$log"; then
    echo "agent integration failed; see $log" >&2
    grep "\[vsrViewer\]" "$log" >&2 || true
    exit 1
  fi
  sleep 0.2
done
[[ -n "$descriptor" && -f "$descriptor" ]] || {
  echo "no MCP descriptor after ${timeout_s}s; see $log" >&2
  exit 1
}

# Record the descriptor path (never its contents) for mcp-proxy.sh.
tmp="$(mktemp "$record.XXXXXX")"
printf '%s\n' "$descriptor" >"$tmp"
mv "$tmp" "$record"

# The `vsr` server's config is fixed: it always runs mcp-proxy.sh, which
# reads the record at each (re)connect. Only rewrite .pi/mcp.json (and ask
# for /reload) when it does not already say so.
pi=reconnect
if [[ ! -f "$mcp" ]] ||
  ! jq -e --arg cmd "$entry" \
    '.mcpServers.vsr.command == $cmd and ((.mcpServers.vsr.args // []) == [])' \
    "$mcp" >/dev/null; then
  tmp="$(mktemp "$mcp.XXXXXX")"
  if [[ -f "$mcp" ]]; then
    jq --arg cmd "$entry" \
      '.mcpServers.vsr = ((.mcpServers.vsr // {}) + {command: $cmd, args: [], exposure: (.mcpServers.vsr.exposure // "direct")})' \
      "$mcp" >"$tmp"
  else
    jq -n --arg cmd "$entry" \
      '{mcpServers: {vsr: {command: $cmd, args: [], exposure: "direct"}}}' >"$tmp"
  fi
  mv "$tmp" "$mcp"
  pi=reload
fi

echo "PID=$pid"
echo "LOG=$log"
echo "DESCRIPTOR=$descriptor"
echo "PI=$pi"
