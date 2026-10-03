# 12 — Demonstrate the live queen's-eye capture workflow

**What to build:** Query the actual view and queen/king bounds, derive an external camera, capture 1280×720 at supported 256 samples, and save checkmatte.png through MCP/proxy without changing the main view or using offline workarounds.

**Blocked by:** 03, 09, 10, 11
**Status:** ready-for-agent
**Milestone:** 1
**Specification:** [Camera & Capture](../spec.md), all Testing Decisions and End-to-end acceptance.

## Acceptance criteria

- [ ] A portable independently worked fixture completes view info → bounds → camera outside the queen → qualified capture → image → opt-in publication, with correct dimensions, pixels, provenance and references.
- [ ] Real loopback MCP/trusted stdio flow uses existing status/cancel/current ownership. No Lua, source/process inspection, asset reload, offline image, server path write, aesthetic-only oracle or absent test match.
- [ ] Verify unchanged scene/main camera/manipulator/renderer/dimensions/accumulation, truthful activity across success/failure/cancel and owned image-buffer lifetime.
- [ ] Imported-chess viewer acceptance uses isolated configuration/private authenticated display, actual backend selection, responsiveness and owned-process cleanup; optional missing assets do not excuse portable acceptance.
- [ ] Broad-editing/session/render/restore/cursor/budget/numeric regressions and CPU/CUDA/disabled builds pass; capture qualification is not replaced by algorithm proof.
- [ ] Guidance matches actual fields, units, capabilities, limits, unavailable behavior, save authority and camera bindings; record exact commands/results/limitations.
- [ ] Required implementation slices are complete or have explicit blocking holes. Do not claim acceptance while required backend/quality/lifetime contracts remain unresolved.

## Out of scope

Unspecified features, persistent camera editing, server filesystem, broader material writes, architecture cleanup, global user-configuration changes and remote publication.

## Comments
