# Addendum 02 — Integration module layout, PNG encoding and dependencies

**Extends:** spec §1 (Framework boundary; public package and dependency selection are implementation choices) and §12 (PNG normalization). **Source:** holes 19, 43, 85. **Decided by:** orchestrator.

## Decisions
- **Build layout (19):** `vsr_integration` compiles sources from its subdirectories, using a recursive glob or an explicit list. Header-only subdirectory modules remain acceptable. Future tickets may move definitions into `.cpp` files but are not required to.
- **PNG encoding (43):** integration PNGs use real deflate compression. `vsr_integration` may link the vendored `vela_ext_stb_image` privately and use its PNG writer. Output stays byte-deterministic for a given input and remains RGBA8 sRGB straight-alpha with a top-left origin. The retained-image budget (32 MiB) is unchanged.
- **Context adapter (85.1):** the adapter stays a header-only template over a Context-shaped type. `vsr::app::Context` does not own or depend on the integration, and `vsr_app` builds with the integration off.
- **Headless host (85.2–3):** the tutorial host must start the loopback MCP listener when built with MCP, publish the descriptor and print only its path. It must also register one fixed-camera helide view with real synchronization hooks. With that, the headless host can serve the §14 gate-5 workflow like the viewer.

## Why
The previous state was fixed while shared files were frozen during concurrent waves, not by design. Uncompressed PNGs held only about 8 maximum-size images in the 32 MiB budget.
