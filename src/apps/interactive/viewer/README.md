## vsrViewer

`vsrViewer` is the main interactive VSR application.

It is used to:

- Load scene data from many supported file formats.
- Inspect and edit objects, layers, parameters, animations, and transfer
  functions.
- Compare ANARI devices and renderer behavior in one UI.
- Save and reload full VSR sessions (`.vsr`) for offline inspection.

## Command-Line Usage

```bash
./vsrViewer [viewer-options] [scene-options]
```

CLI parsing is split into two layers:

- Viewer options are consumed by `vsr::ui::imgui::Application`.
- Scene Archive and import options are consumed by `vsr::app::Context`.

### Parsing Rules

- If no Archive or importer flag is active, a positional filename is treated
  as a state file (`.vsr`) to load.
- If an importer flag is active, following filenames are loaded with that
  importer until another importer flag appears.
- `-l` or `--layer` changes the target layer for subsequent imports.
- If no files are provided and no state file is loaded, `vsrViewer` generates a
  default material-orb scene.

## Viewer Options

| Option | Description |
| --- | --- |
| `--agentIntegration` | Explicitly expose authenticated agent tools, including existing-node edits; requires an integration/MCP-enabled build. | 
| `--agentRestorationBytes N` | Set the launch-local shared recovery soft target (positive uint64 decimal bytes); default **4294967296** (4 GiB). Does not enable integration or camera control. |
| `--agentRestorationHardBytes N` | Set the single-entry recovery hard cap (at least soft); default **8589934592** (8 GiB). Above-soft entries at most hard retain alone; above-hard approval is not implemented. Neither limit preallocates memory. |
| `--noDefaultLayout` | Do not apply built-in ImGui dock layout at startup. |
| `--noDefaultRenderer` | Do not auto-select a default renderer in the viewport. |
| `--secondaryView <library>` / `-sv <library>` | Enable secondary viewport and set its ANARI library. |

## Scene Archive and Import Options

| Option | Description |
| --- | --- |
| `-v`, `--verbose` | Enable verbose logging. |
| `-e`, `--echoOutput` | Enable log echo output mode. |
| `-vsr <file>` | Load a native VSR Scene Archive. |
| `-l <name>`, `--layer <name>` | Set destination layer for following imports. |
| `-camera <file>`, `--camera <file>` | Provide camera file path (if used by workflow). |
| `-ensight_fields <f1,f2,...>` | Field selection list used with `-ensight`. |
| `-xf <file>`, `--transferFunction <file>` | Load transfer function file for subsequent `-volume` import(s). |
| `-blank` | Start with an empty scene (no generated default scene). |

## Foreign Importer Flags

Use one of these flags before filenames to select how those files are loaded:

- `-agx`
- `-assimp`
- `-assimp_flat`
- `-axyz`
- `-dlaf`
- `-e57xyz`
- `-ensight`
- `-gltf`
- `-hdri`
- `-hsmesh`
- `-mini` (miniScene; needs `VSR_USE_MINISCENE=ON`)
- `-nbody`
- `-obj`
- `-pdb`
- `-ply`
- `-pointsbin`
- `-pt`
- `-silo`
- `-smesh`
- `-smesh_animation`
- `-swc`
- `-trk`
- `-usd`
- `-usd_mtlx` (USD, emitting native MaterialX materials rather than a portable
  mapping; falls back per material where no MaterialX network converts)
- `-vtp`
- `-vtu`
- `-xyzdp`
- `-volume`

Note: Availability of some importers depends on CMake feature options and
optional dependencies.

## Examples

Load a saved VSR session:

```bash
./vsrViewer state.vsr
```

Load geometry and an HDRI environment:

```bash
./vsrViewer -obj model.obj -hdri studio.exr
```

Load multiple files into explicit layers:

```bash
./vsrViewer -l points -ply cloud.ply -l mesh -obj part.obj
```

Load a volume with a transfer function:

```bash
./vsrViewer -xf fire.xf -volume density.nvdb
```

Start secondary viewport on another ANARI library:

```bash
./vsrViewer -sv visgl -gltf scene.gltf
```

## Live agent placement editing

With `--agentIntegration`, **Agent Activity** shows the private descriptor path,
operation outcomes, batch references, and conditional restoration eligibility.
Use `vsrMcpProxy` to connect a trusted client; do not paste descriptor contents
or credentials into a prompt. Scene objects own parameters, while layer nodes
address individual placements; readable parameters are not automatically writable.

See the [live placement guide](../../../../docs/live-placement-editing.md) for
filtered/paged inspection, copying `node_edit_version` into `version`, required
structural/world ancestry tokens, local/world origin moves, VSR degree rotations,
full affine matrices, conflicts, and exact one-way Restore. Object parameter
editing uses generic stored-type and introspected-metadata validation, not a
material/light allowlist. Inspection entries expose an `editable` Boolean,
typed `min`/`max` values and `string_values` choices when present; these are
value-support observations, not authority grants.

Existing parameters support stored `BOOL` as JSON Boolean, `STRING` without
embedded NUL (enum choices enforced), signed/unsigned 8-, 16-, 32-, and 64-bit
integer scalars/vec2/vec3/vec4 as integral JSON numbers within both type range
and `[-(2^53-1), 2^53-1]`, and float32/float64 scalars/vec2/vec3/vec4 as
finite JSON numbers within type range. Vectors require exactly the stored
component count. Matching `min`/`max` metadata applies per component; `COLOR`
usage retains `[0,1]`. Parameter `enabled` is preserved and does not restrict
value editing. References, object parameter matrices, other types, and
parameter creation are unsupported. The legacy `values` mirror covers generic
supported float32 scalar/vec2/vec3/vec4 values, not exclusive edit authority.
Existing authority, atomicity, versioning, and conditional restore are
unchanged. Editing a stored scene camera does not change the viewport or agent
capture camera; the guide lists deferred tools.

## User Color Maps

The transfer-function editor loads optional RGB color-map presets once at
startup from the User Config Directory:

- Linux/macOS: `~/.config/vela/colormaps/` (`$XDG_CONFIG_HOME/vela/colormaps/`
  on Linux when set)
- Windows: `%APPDATA%/vela/colormaps/`

Only `.1dt` files are loaded. The filename stem becomes the color-map name in
the editor, so `Magma Soft.1dt` appears as `Magma Soft`. Files are loaded in
alphabetical order after the built-in palettes; if a user file has the same
name as an earlier palette, it replaces that palette.

Each non-comment line should contain whitespace-separated `r g b a` values.
The auto-loaded presets use only RGB values; alpha is ignored so selecting one
does not change the current opacity curve. Manual transfer-function load/save
continues to use `.1dt` as full RGBA transfer-function state.

## Environment

`VSR_ANARI_LIBRARIES` controls the ANARI library list shown in the UI (comma-
separated names). If unset, `vsrViewer` falls back to built-in defaults.

With `--agentIntegration`, the agent's independent view renders on the first
`VSR_ANARI_LIBRARIES` entry; when that variable is unset or its first entry
is `environment`, on `ANARI_LIBRARY`; otherwise on `helide`. An agent can
switch it to any entry of that list with `vsr_select_device`.
