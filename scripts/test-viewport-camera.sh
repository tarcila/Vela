#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

# Run only the opted-in real viewport/controller test, never a user's viewer.
# Build/install first. An installed ANARI library can override build-tree lookup.
build=$(realpath "${1:-build-agent}")
display=${VSR_CAMERA_TEST_DISPLAY:-93}
run=$(mktemp -d "$build/camera-live.XXXXXX")
mkdir "$run/config" "$run/runtime"
chmod 700 "$run" "$run/config" "$run/runtime"
xvfb=
cleanup() {
  if [[ -n "$xvfb" ]]; then
    kill "$xvfb" 2>/dev/null || true
    wait "$xvfb" 2>/dev/null || true
  fi
  rm -rf -- "$run"
}
trap cleanup EXIT

# Explicit authenticated TCP avoids dependence on a user's Wayland runtime
# or host/container Unix-socket namespace. No existing display is reused.
xauth -f "$run/auth" add "localhost:$display" . "$(openssl rand -hex 16)"
Xvfb ":$display" -auth "$run/auth" -screen 0 800x600x24 \
  -listen tcp -nolisten unix >"$run/xvfb.log" 2>&1 &
xvfb=$!
ready=false
for attempt in {1..100}; do
  if ! kill -0 "$xvfb" 2>/dev/null; then
    break
  fi
  if DISPLAY="localhost:$display" XAUTHORITY="$run/auth" xset q >/dev/null 2>&1; then
    ready=true
    break
  fi
  sleep 0.1
done
if [[ "$ready" != true ]]; then
  printf 'Isolated Xvfb failed to become ready; select another VSR_CAMERA_TEST_DISPLAY\n' >&2
  exit 1
fi

export VSR_VIEWER_TEST_ISOLATED=1
export XDG_CONFIG_HOME="$run/config"
export XDG_RUNTIME_DIR="$run/runtime"
export DISPLAY="localhost:$display"
export XAUTHORITY="$run/auth"
export SDL_VIDEODRIVER=x11
export SDL_RENDER_DRIVER=software
export LD_LIBRARY_PATH="$build/install/lib:$build:${LD_LIBRARY_PATH:-}"
timeout 120 "$build/vsrIntegrationTests" '[.viewer-camera]'
