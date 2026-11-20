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
- `-anari_capture` (an ANARI Capture written by the ANARI-SDK `capture`
  device; its `blobs/` directory must sit beside it)
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
- `-usd_mdl` (USD, emitting native MDL materials rather than a portable
  mapping; falls back per material where no MDL network is authored)
- `-usd_variant <set>=<variant>` (not an importer: selects `<variant>` in every
  prim's `<set>` Variant Set for the USD files named after it, before they
  load; repeat it to select in several sets)
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

Load a USD Stage with its lower-resolution material variant:

```bash
./vsrViewer -usd_variant material=renderLow -usd shot.usda
```

Load multiple files into explicit layers:

```bash
./vsrViewer -l points -ply cloud.ply -l mesh -obj part.obj
```

Load a volume with a transfer function:

```bash
./vsrViewer -xf fire.xf -volume density.nvdb
```

Import one principled participating medium (requires a device advertising
`principled`):

```bash
./vsrViewer -principled_volume name=smoke density=smoke.nvdb \
  density:value=0.5 scattering=0.5,0.5,0.5 \
  temperature=heat.nvdb blackbodyIntensity=1
./vsrViewer -principled_volume_json medium.json
```

Each `-principled_volume` starts one medium. Assignments have the form
`property[:field|value]=value`; a complete numeric scalar or comma-separated RGB
vector infers `value`, otherwise a field-capable property infers `field`.
Use `density:field=123` for a numeric filename. A field and its multiplier value
can coexist; duplicate assignments to either semantic are errors. Existing
importer flags end the block. JSON descriptions do not accept CLI overrides.

Field slots are scalar `density`/`temperature` and scalar or linear-RGB
`scattering`/`emission`. Scalar samples broadcast across RGB; RGB samples
multiply componentwise. VTI retains its scalar component splitting. No transfer
function, normalization or resampling is applied. `.vdb` means NanoVDB, not OpenVDB.
Use `bounds=xmin,ymin,zmin,xmax,ymax,zmax` to override automatic union bounds;
uniform media and fields without derivable domains (currently FLASH AMR)
require explicit finite, positive bounds. Values multiply bound fields.
Emission defaults to `0,0,0` and blackbody intensity to `0`, even with fields
attached; enable them explicitly when wanted.

`scattering` is single-scattering albedo in [0, 1], not a scattering coefficient.
Total extinction is `density * -log(transmissionColor) / transmissionDistance`;
scattering and absorption are its albedo-weighted portions. `transmissionColor`
is in (0, 1] and `transmissionDistance` must be positive when extinction is
nonzero. Density is zero outside its field's sampling domain; scattering,
emission and temperature fall back to their constants outside their domains.
Inactive sparse voxels inside the domain use the field background instead.

Direct emission is radiance per unit world-space length, independent of density.
Thermal emission is absorption times `blackbodyTint * blackbodyIntensity *
B(temperature)`, so zero density or white effective scattering albedo disables
it. Temperature is in kelvin (constant times field), with nonpositive values
producing no thermal emission. B is absolute Planck radiance integrated over
360–830 nm against the CIE 1931 2-degree observer, scaled so a D65-shaped spectrum
of unit band radiance has Y=1, then converted to linear Rec.709/sRGB without
chromatic adaptation; negative RGB is clamped while preserving Y.

These are the intended optical semantics, not transport implemented by Vela.
The current experimental VisRTX API shell accepts these parameters but omits
the volume from rendering with a diagnostic, even when it advertises `principled`.

```json
{
  "name": "smoke",
  "fields": {
    "density": {"file": "smoke.nvdb"},
    "temperature": {"file": "heat.nvdb"}
  },
  "parameters": {"density": 0.5, "scattering": [0.5, 0.5, 0.5], "blackbodyIntensity": 1}
}
```

JSON paths resolve relative to the description file. Optional `bounds` is a
six-number array; RGB parameters are three-number arrays. The importer UI can
read JSON into editable mappings and export a description with absolute source
paths. Animation sequences are not supported by these flags.

Start secondary viewport on another ANARI library:

```bash
./vsrViewer -sv visgl -gltf scene.gltf
```

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
