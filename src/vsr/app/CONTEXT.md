# VSR App

VSR App composes reusable application state around VSR scenes, animations,
rendering, and interaction. It owns application-level persistence without
making the lower-level VSR I/O library depend on application concepts.

## Language

**Application Dump**:
A native application-level file snapshot containing required Scene and
Animation Manager Archives plus other application state; individual
applications may extend it. Application Dumps belong to VSR App rather than
VSR I/O, and reconstruction loads the scene before animations that bind to it.
_Avoid_: Context dump, scene dump, archive

### Application-owned state

**Vela**:
The product: the applications a user installs and runs. It is distinct from
VSR, the libraries and file formats those applications are built on.
Per-user state belongs to Vela; library and format names stay VSR.
_Avoid_: VSR (for the product)

**User Config Directory**:
The one per-user directory where Vela keeps everything it remembers
between runs: Application Preferences, each application's UI State, and
the user's own color maps and scripts.
_Avoid_: VSR config directory, settings folder

**UI State**:
How one application's interface is arranged and presented: its dock layout
and each window's presentation settings. It belongs to the application, never
to a document, so opening an Application Dump or a Project leaves it alone,
and it never travels between a Studio client and server.
_Avoid_: Layout (for the whole), Client Layout, session, workspace

**Dock Layout**:
The part of UI State that records where windows sit and how they are docked.
_Avoid_: ImGui ini, layout file

**Default Layout**:
The built-in Dock Layout an application starts with when it has no saved UI
State, and returns to on request.

**Application Identifier**:
The stable name an application declares so its UI State is kept apart from
every other application's.
_Avoid_: Executable name, window title

**Application Preferences**:
Settings shared by every VSR application, such as the ANARI devices on offer
and the font scale, saved only when the user asks.
_Avoid_: App settings, defaults (for the file)

## Frame Channel selection persistence and tools

`FrameChannelState.h` saves/loads a `FrameChannelSelection` into a subtree
chosen by the caller. Offline settings use `offlineRendering/channelSelection`
in an Application Dump; a Viewport may use the same contract in its application's
UI State. The helper does not copy UI State into a dump. Persisted fields are
`deviceName` (the complete advertised identity), `visualization`, `rangePolicy`
(`auto`/`fixed`), `rangeMin`, `rangeMax` and `invertEdges`. Catalog order and
resolved internal pixel storage are not persisted.

Named fields take precedence over legacy AOV fields, even when the latter are
invalid. Without named fields, NONE maps to color/Color, DEPTH to depth/grayscale,
ALBEDO to albedo/Color, NORMAL to normal/normal colors, EDGES to objectId/Edges,
and the three identity modes to their corresponding channel/ID colors. Explicit
legacy depth bounds become Fixed; absent bounds leave Auto. Edge inversion is
preserved. Unknown enums, malformed named fields and invalid Fixed ranges return
an explanation without changing the caller's selection. `LegacyFrameChannelKeys`
allows UI callers to supply their existing key spellings. Interactive callers
can explain a failure and restore Color. Offline settings defer numeric range
validation until CLI overrides are merged: vsrRender can repair invalid saved
Fixed bounds with Auto or valid replacement bounds, but rejects unusable effective
intent and unrelated malformed saved fields. Structural state validation remains
strict; the default state-loader behavior also validates ranges.
The legacy `aov` settings remain available for standard-channel export consumers.

Both tools use the shared option parser and effective-selection resolver.
Offline resolves semantic/type defaults without saved visualization precedence;
vsrRender preserves compatible saved visualization intent. Parser failures name
the requested channel, even when --channel follows the malformed option, and
point to --list-channels. Both tools document the same mode spellings in their
shared help: `color`,
`grayscale`, `normal`, `id-colors`, `edges`, `component-x`, `component-y`,
`component-z`, `component-w`, `magnitude`. Only modes listed as compatible for the
selected channel are accepted; Edges is a visualization of objectId, not a channel.
Channel arguments are exact presentation names (only one leading `channel.` is
removed). Use `--list-channels` to discover supported names and types; listing
uses the selected device/renderer and produces no image.

Examples:

```sh
vsrRender saved.vsr --list-channels
vsrRender saved.vsr --channel depth --range fixed --range-min 0 --range-max 10
vsrRender saved.vsr --channel motionVectors --visualization magnitude --range auto
vsrOffline --lib helide --channel normal --visualization normal -o normals.png
```

vsrRender retains its saved-state positional argument, sampling and PNG camera
pose/animation output workflow. No options preserves saved selection intent.
Explicit options replace the corresponding saved settings; a channel change
retains a compatible saved visualization, otherwise selecting the new channel's
default and clearing incompatible range/inversion settings. Fixed CLI range
requires both finite bounds with minimum < maximum; Auto accepts no bounds.
vsrOffline starts at Color and Auto, retaining its existing importer/output
options. Both tools use the shared source and Channel Visualization pass, preserve
file orientation, and fail nonzero with channel/device/renderer and listing
context when selected output cannot be produced. No image is written for that
failed frame; previously completed animation frames are not rolled back.
