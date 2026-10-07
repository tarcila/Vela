# Isolated live chess acceptance

`scripts/test-chess-feature.py` exercises the installed **actual vsrViewer** through
its fresh authenticated launch descriptor and normal loopback MCP tools. It does
not attach to or restart an existing viewer, reuse a user's X display or change
Pi configuration/preferences. Build and install first; the viewer's ANARI library
path starts with that build's installation, excludes the old pi-integration
installation and retains ANARI device dependencies.

The machine-local input assets are:

- `/data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf`
- `/data/Envmaps/footprint_court.hdr`

Pillow, Xvfb, xauth, xset and XTest are required. The driver creates a private
0700 runtime/configuration and authenticated Xvfb `-noreset` at an unused display.
Neither launch credentials nor X authentication cookies are saved in evidence.

```bash
cmake --build build-agent --parallel 2
cmake --install build-agent --prefix "$PWD/build-agent/install"
# EVIDENCE must be a workspace-local scratch directory, not a source artifact.
python3 scripts/test-chess-feature.py build-agent "$EVIDENCE" \
  scripts/chess-feature-ui.json --agentRestorationBytes 67108864
```

For the actual CUDA-enabled multi-config rig, reuse its configured build and
installation; do not substitute a CPU configure:

```bash
cmake --build _out/_cmake --config RelWithDebInfo --parallel 2
cmake --install _out/_cmake --config RelWithDebInfo
python3 scripts/test-chess-feature.py _out/_cmake "$EVIDENCE" \
  scripts/chess-feature-ui.json --config RelWithDebInfo \
  --install-prefix "$PWD/_out" --agentRestorationBytes 67108864
```

`--config` selects the executable subdirectory; `--install-prefix` selects the
newly installed ANARI library. Both are driver-only provenance/path options,
not host configuration, device selection or qualification switches. The driver
records and hashes the selected executables and installed library. A CUDA-enabled
host using the qualified Helide CPU-ready presentation still proves only that
presentation path, not GPU presentation.

The acceptance driver **defaults to no override**: without the option the host
retains its 16777216-byte (16 MiB) Activity/restoration cap. The command above
explicitly selects the user-authorized 67108864-byte (64 MiB) host cap, needed
for this original 2048×2048 RGBA Image plus Activity overhead. The driver records
the exact option and effective metadata in `provenance.json`, and checks that
session/transient/file ceilings remain unchanged. Production Import never raises
limits; this is launch configuration, not a persistent preference or permission.
`scripts/test-agent-host-options.py BUILD EVIDENCE_DIR` separately checks both
hosts' malformed-option rejection and headless default/explicit metadata/binding.

The UI plan applies only to the fresh private 1280×900 default layout. It uses the
existing Agent Activity camera/edit controls and Camera Poses recovery button,
not an alternate permission API. Without the plan, bounded checkpoints accept
`click`, `drag`, `resize`, `screenshot` and `continue` arrays in
`$EVIDENCE/ui-command.json`; the driver renews the session during those waits.
Screenshots are UI-action diagnostics, **not** presented-frame evidence.

## What is checked

- Paged discovery and exact live inspection, Sampler/Array metadata and real
  Sampler → Material → Surface → eight black pawn placement sharing relationships.
- Matrix R/G row swap, independently computed asymmetric/neutral colour oracles,
  committed status, reinspected rows, matching main-viewport images and exact
  ordinary batch restore. No whole-piece colour offset is substituted.
- Actual black Image Export and separate external PIL processing by
  `scripts/recolour-chess-image.py`, preserving raw dimensions/channels/alpha,
  neutral dark texels, Color Space and Row Order. Import uses its observed token,
  original Array and confined processed basename. All independent memory/session/
  file caps remain unchanged; only the explicitly configured Activity cap differs.
- A successful Image import must identify the complete discovered shared-Sampler
  set, keep every binding on that original Array, produce exact processed bytes,
  and show a qualified post-import frame. Normal exact Restore must publish a
  fresh token agreeing with export/inspection, preserve all metadata and bindings,
  recover every original byte and show a qualified restored frame. An actual resource rejection
  instead records `blockers.json`, verifies unchanged bytes/token and explicitly
  leaves this workflow incomplete; it is never credited as shared-user mutation.
- Camera starts off; the separate local camera checkbox enables only the explicit
  main interactive view. Disable edits rejects matrices/import but permits READ
  Export and separately permitted camera movement. Kinds/isError and strict
  Import/Restore schemas are checked at the actual MCP boundary.
- The authored board is inspected rather than treated as an opening. A private
  recoverable node batch stages Fool's Mate using measured square spacing and
  observed world origins. Queen-side eye looks toward the defeated white King.
  The driver restores the exact authored placement matrices afterward.
- The presented PNG must match the move receipt's **exact** view identity,
  rendered/current view revision, chosen Scene revision and rendered camera/FOV
  at `anari_render_start`, with `stale=false`. Fresh request IDs poll for at most
  120 seconds. Superseded targets or timeouts fail; independent rendering is
  never substituted. The actual unqualified independent frame remains unavailable.
- The existing Before agent UI button recovers the prior pose/projection/FOV and
  advances revision. Individual real orbit, pan and zoom gestures must each
  change their effective camera field/revision. Original glTF/JPEG/bin/HDR hashes
  remain unchanged; normal exit removes this launch's descriptor/exchange.

Exit **0** and `PASS` mean every combined assertion passed. Exit **2** means a
recorded incomplete acceptance (currently the actual 2048×2048 RGBA snapshot
cannot fit the default 16 MiB global retention cap plus Activity overhead).
Exit **1** means an unexpected failed assertion/driver error. Never turn exit 2
into a passing test, increase host budgets automatically (an explicit launch
option is required), resize the target or
substitute a tiny fixture. PNG cleanup does not free undo; undo eviction/restore
also does not delete PNGs.

This script uses only the existing CPU-ready checked SDL upload/presentation
qualification. CUDA-enabled build/device-memory parity tests are separate gates;
they do not broaden GPU presented-frame qualification. Complete applicable serial
CTest and the independent two-axis review are still required for feature closure.
