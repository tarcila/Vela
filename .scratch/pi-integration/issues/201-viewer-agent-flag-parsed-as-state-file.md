# 201 — vsrViewer `--agentIntegration` swallows scene inputs

**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Files:** src/vsr/app/Context.cpp, src/apps/interactive/viewer/vsrViewer.cpp

**Kind:** extends

## Observed (ticket 18, 2026-09-29)
`vsrViewer --agentIntegration -obj /tmp/t18-fixture.obj` (and plain `vsrViewer --agentIntegration`) exposes a scene with only a default material and camera. It has no geometry and no generated icosphere, so every agent render is black.

## Likely cause (not verified by a fix)
`Context::parseCommandLine` has no case for `--agentIntegration`. When no importer is pending, the unknown argument lands in the `else` branch that sets `commandLine.stateFile` and `loadedFromStateFile = true`. `setupSceneFromCommandLine` then skips both the default scene and `loadCommandLineSceneInputs()`. vsrViewer reads the flag from `argv` itself but does not remove it before Context parses the command line.

## Acceptance
- [x] `vsrViewer --agentIntegration -obj <file>` imports the file.
- [x] `vsrViewer --agentIntegration` alone generates the default scene.

### 2026-09-28 orchestrator
Bug under addendum 05 item 4 (viewer parity); no new decision needed. ready-for-agent.

## Comments

### 2026-09-29 iteration — resolved
Confirmed cause. vsrViewer `main()` now strips `--agentIntegration` from argv and passes the flag to `Application` explicitly, so Context never sees it (and never takes it for a state file). Context.cpp is unchanged, and vsr_app stays free of integration code.
Verified: `cmake --build _out/_cmake --config RelWithDebInfo --target vsrViewer` → ok. Under `Xvfb :202`, `DISPLAY=:202 XDG_RUNTIME_DIR=/tmp/t201-rt LD_LIBRARY_PATH=<RelWithDebInfo> vsrViewer --agentIntegration -obj /tmp/t201.obj`, then vsrMcpProxy vsr_inspect scene 0 → terminal ok, with ANARI_SURFACE "t" and ANARI_GEOMETRY triangle "t". Plain `vsrViewer --agentIntegration` → inspect lists the generated "icosphere" surface and its arrays. All started processes were killed. (A reused Xvfb display sometimes failed SDL init on the second client; a fresh `-noreset` server fixed it.)
Holes filed: none
