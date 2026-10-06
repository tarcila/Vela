# Exchange agent images through a launch-scoped directory

Image Export and Image Import for agent integration move texels as PNG files in
a directory the viewer owns, `$XDG_RUNTIME_DIR/vsr/<launch_id>/images/` (0700),
and MCP requests and responses carry only file names inside it — never
arbitrary paths and never inline pixel data. This deliberately departs from the
rendering tools, which return PNGs only as MCP image content.

Inline transport was rejected because the agent's image tooling (ImageMagick,
PIL) runs in a shell that cannot receive MCP image content, a codemode sandbox
that cannot write files sits between the two, and a large Image as base64 PNG
approaches the 12 MiB response ceiling. Arbitrary paths were rejected because
the viewer would then read and write wherever an agent pointed it; confining
both directions to one launch-scoped directory keeps the viewer the sole
creator and validator of what it reads, and lets it discard the directory with
the launch. The agent and viewer already share this filesystem — the launch
descriptor lives beside it — so no new deployment assumption is introduced.

ADR 0031's no-wire-file-transfer rule governs the SciVis Studio client-server
protocol and is unaffected: this directory is local to one host and never
crosses a network.
