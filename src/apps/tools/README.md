## Command-Line Tools

This directory contains utility executables for data conversion, scene inspection,
headless rendering, and scripting.

## Tools Overview

| Tool | Purpose | Usage |
| --- | --- | --- |
| `obj2header` | Convert a Wavefront OBJ mesh into a generated C/C++ header with packed vertex arrays. | `./obj2header <file.obj> <outfile.h>` |
| `vsrPrint` | Print a `.vsr` file as the Data Tree Text Encoding. | `./vsrPrint <file.vsr>` |
| `vsrConvert` | Convert a `.vsr` file between the Binary and Text Encodings. | `./vsrConvert [--text\|--binary] <input.vsr> <output.vsr>` |
| `vsrRender` | Offline render from a saved `.vsr` state file (including render settings and camera/animation data). | `./vsrRender <state_file.vsr>` |
| `vsrOffline` | Headless renderer with direct CLI control over imports, camera, lights, renderer, and output. | `./vsrOffline [options]` |
| `vsrVolumeToNanoVDB` | Convert supported structured volume files to NanoVDB (`.vdb`) with optional quantization settings. | `./vsrVolumeToNanoVDB [options] <input_volume> <output.vdb>` |
| `vsrLua` | Lua scripting and REPL tool for scene import, manipulation, and rendering workflows. | `./vsrLua <script.lua> [args...]`, `./vsrLua -e "<lua code>"`, `./vsrLua -i` |

| `vsrMcpProxy` | Trusted stdio MCP adapter that lets Pi attach to a running VSR application's loopback MCP endpoint. | `./vsrMcpProxy <descriptor.json>` or `VSR_MCP_DESCRIPTOR=<descriptor.json> ./vsrMcpProxy` |

`vsrLua` is only built when `VSR_USE_LUA=ON`. `vsrMcpProxy` is only built when
`VSR_USE_INTEGRATION_MCP=ON`.

## `obj2header`

Converts OBJ triangle data into a header file containing:

- `vertex_position`
- `vertex_normal`
- `vertex_uv`

Usage:

```bash
./obj2header model.obj generated_model.h
```

## `vsrPrint`

Loads a `.vsr` file into `vsr::core::DataTree`, in whichever encoding it is
in, and prints it as the Text Encoding (see
[`src/vsr/core/DataTreeText.md`](../../vsr/core/DataTreeText.md)).

Usage:

```bash
./vsrPrint scene.vsr
```

## `vsrConvert`

Converts a Data Tree file between the Binary and Text Encodings. The input's
encoding is detected from its contents, never from its extension, and the
output is written in the other encoding unless `--text` or `--binary` forces
one. Forcing the input's own encoding re-saves the file in canonical form,
which is a cheap way to normalize a hand-edited text file. Both encodings
use the `.vsr` extension; a different suffix for text files is a naming
convention the tool neither needs nor checks.

Usage:

```bash
./vsrConvert cameras.vsr cameras_text.vsr      # binary -> text, ready to edit
./vsrConvert cameras_text.vsr cameras.vsr      # text -> binary
./vsrConvert --text edited.vsr canonical.vsr   # text -> canonical text
```

## `vsrRender`

Renders from a saved VSR state file. It loads scene content and offline render
settings from the file and writes PNG frames named `vsrRender_0000.png`,
`vsrRender_0001.png`, etc. If the scene has keyframed animation, it renders one
image per animation frame.

Usage:

```bash
./vsrRender state_file.vsr
```

## `vsrOffline`

General-purpose headless renderer. It uses the same importer command-line model
as `vsr::app::Context` and adds offline-rendering controls (resolution, samples,
camera, lights, and output).

Usage:

```bash
./vsrOffline [options]
```

Common options:

- `-w, --width <int>` frame width (default `1024`)
- `-h, --height <int>` frame height (default `768`)
- `-s, --samples <int>` samples per pixel (default `128`)
- `-o, --output <file>` output image path (default `vsrOffline.png`)
- `--lib <name>` ANARI library
- `--renderer <name>` renderer subtype (default `default`)
- `--camera <name-or-index>` select a scene camera by exact name or object index
- `--campos <x y z>`, `--lookpos <x y z>`, `--upvec <x y z>`, `--fovy <float>`
- `--aperture <float>`, `--focus <float>`
- `--bg-color <r g b a>`, `--no-bg`
- `--ambient <float>`, `--ambient-color <r g b>`
- `--dir-light <dx dy dz> <r g b> <intensity>`

If `--camera` is omitted, or if the requested camera is invalid, `vsrOffline`
prints the available scene cameras and prompts on stdin for a selection. If the
scene has no cameras, it creates a default perspective camera and frames it
from the scene bounds. `--camera` is mutually exclusive with manual camera
override flags (`--campos`, `--lookpos`, `--upvec`, `--fovy`).

Use `-vsr <file>` to load a Scene Archive. Foreign-format importer flags
include `-gltf`, `-obj`, `-ply`, `-volume`, `-hdri`, `-silo`, `-usd`,
`-usd_mtlx`,
`-assimp`, `-axyz`, `-e57xyz`, `-pdb`, `-swc`, `-trk`, `-nbody`, and
`-l`/`--layer`.

Example:

```bash
./vsrOffline -gltf scene.glb -w 1920 -h 1080 -s 256 -o render.png
```

With `--export-mini <file>` (builds with `VSR_USE_MINISCENE=ON`), `vsrOffline`
loads the scene the same way but writes it to a miniScene `.mini` file instead
of rendering; no ANARI device is loaded and the rendering, camera, and light
options are ignored. See
[ADR 0042](../../../docs/adr/0042-export-miniscene-in-the-form-haystack-consumes.md)
for what the export keeps.

```bash
./vsrOffline -obj model.obj --export-mini model.mini
```

## `vsrVolumeToNanoVDB`

Converts a volume file to NanoVDB and supports handling undefined values,
quantization precision, and dithering.

Usage:

```bash
./vsrVolumeToNanoVDB [options] <input_volume> <output.vdb>
```

Options:

- `--undefined <value>`, `-u <value>` skip voxels matching an undefined value
- `--precision <type>`, `-p <type>` choose `fp4|fp8|fp16|fpn|half|float32`
- `--dither`, `-d` enable quantization dithering

Supported input formats include `.raw`, `.vti`, `.vtu`, `.mhd`, `.hdf5`, and
`.nvdb`.

Example:

```bash
./vsrVolumeToNanoVDB --undefined 0.0 --precision fp8 --dither input.mhd output.vdb
```

## `vsrLua`

Lua front end for scripted VSR workflows. It pre-creates a scene named `scene`
for scripts and provides:

- Batch script execution
- Inline code execution (`-e`)
- Interactive REPL (`-i`)

Usage:

```bash
./vsrLua script.lua [args...]
./vsrLua -e "print(scene:numberOfObjects(vsr.GEOMETRY))"
./vsrLua -i
```

In script mode, extra CLI arguments are passed to Lua in the `arg` table.

## `vsrMcpProxy`

A stdio MCP server that forwards to the loopback MCP endpoint published by a
VSR application started with agent integration (for example
`vsrViewer --agentIntegration`). Pi launches it as a stdio MCP server; the
model sees only the application's tools.

The proxy:

- reads and validates the private launch descriptor (owner-private directory,
  0600 file, strict schema) from its argument or `$VSR_MCP_DESCRIPTOR`;
- attaches with the launch credential and keeps the session ID and secret in
  memory only;
- forwards each newline-delimited JSON-RPC message as one POST with the
  `Authorization`, `VSR-Session-ID` and `VSR-Session-Proof` headers;
- renews on its own timer at the cadence the server reports, whether or not
  any messages flow;
- after a lost session (revocation, expiry, reconnect), attaches again as a
  new session and never reuses old authority;
- detaches on stdin EOF, `SIGINT`, `SIGTERM` or `SIGHUP`.

Every forwarded request is bounded. The connect deadline is 5 s, the overall
response deadline is 30 s and the response cap is 16 MiB (headers and body).
The proxy honours `Content-Length`. A request that breaches a limit fails
locally: the stdio client gets a JSON-RPC error whose `error.data.code` is
`timeout`, `response_too_large` or `unavailable`. Renewal and detach use the
same limits, so shutdown never waits longer than those deadlines. If a lost
session cannot be reattached, the request is answered locally with the
attach's frozen code (for example `occupied`) and is not forwarded.

Endpoint ownership (Linux): after a crash, a stale descriptor may name a port
that another local account has since bound. Before sending the launch
credential on each new TCP connection, the proxy looks up the connection in
`/proc/net/tcp` or `/proc/net/tcp6` and checks that the listening peer socket
belongs to the proxy's effective UID. If the owner differs, or the check cannot
be made, the proxy sends nothing, prints a fixed diagnostic without the
credential and exits with status 1.

`vsr/*` methods sent by the stdio client are refused and never forwarded.
Diagnostics on stderr are fixed strings. They never include credentials,
secrets, headers or message bodies.

Pi configuration (an explicit `--mcp-config` file, not a project
`.mcp.json`):

```json
{"mcpServers": {"vsr": {
  "command": "vsrMcpProxy",
  "args": ["/run/user/1000/vsr/vsr-mcp-<launch_id>.json"]
}}}
```

Start the application first. The proxy exits with status 1 when the descriptor
is missing or unsafe, or when the attach fails, for example because another
client already holds the session, or when the endpoint's listener is not
owned by the current user.
