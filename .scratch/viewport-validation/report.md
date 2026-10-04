# Live viewport/MCP validation and improvement proposal

Validation only: no production implementation, configuration edits, scene edits, viewport manipulation, server restart, resynchronization, issues publication or remote actions. Existing `.envrc` and `.pi/mcp.json` changes were preserved. Diagnostic files stay local under this directory.

## Running build and endpoint

Running vsrViewer PID1714916 started2026-10-04 10:10:19 in broad-editing. Its `/proc/.../exe` points to this workspace's installed viewer, with the same inode and SHA256 as the installed file. Installed and RelWithDebInfo build-tree viewer hashes both `e9332489302d0e23c149e0caa16db409923aa07f322bc8b694af34cdfb2574c9`. Installed/build-tree proxy hashes both `26659a32860074cb00eb694d0b06c255e2246a64ef7db141e87271cf72d125cc`. This rules out a stale running viewer relative to those artifacts; no fresh source rebuild was performed. A Ninja dry-run encountered its glob-verification/CMake-regeneration chain, so it is not an independent proof that all source dependencies need no work.

The active proxy connects to the descriptor shown in the viewer's Agent Activity. Descriptor ownerUID1000/mode0600 verified; contents/launch credential were never printed. Existing attached controller was retained, not replaced. Fresh public MCP discover/view-info/bounds/render requests succeed through that controller. An authenticated read-only tools/list request also returned HTTP200 and11tools without attaching another session; credentials stayed in the diagnostic program's memory.

## Confirmed frame/provenance mismatch

A read-only OS X11 capture targeted only the window whose `_NET_WM_PID` matches the viewer; no desktop fallback. `viewer-window.png` is1724x1064 and is **OS diagnostic evidence, not an MCP viewport-frame feature**.

The real viewer HUD reports:
- Device Identifier:barney;
- renderer:default;
- camera label:default (not a complete effective world-pose observation);
- displayed viewport:1340x290;
- render buffer:1005x217.

The displayed scene includes the visible HDRI environment and a composition/aspect unlike the agent image. Source AgentView independently loadshelide, owns its camera/Image Pipeline and fixes640x360. The viewer's source registration remains the separately named agent view. Fresh MCP discovery returned one Scene and one renderable view(id0), not an independently registered interactive viewport.

Fresh vsr_view_info0 returned unavailable/missing_binding, capture unexposed/unqualified_binding, quality_units empty. vsr_bounds0 likewise returned unavailable/missing_binding—not empty geometry. Nevertheless legacyvsr_render0 succeeded and vsr_render_image returned640x360,capture1. The image visually reproduces the independent dark-background agent chess rendering from the handoff. This is compatible legacy rendering, not proof of a fallback or broken viewport materials/HDRI. It is not the actual barney viewport output.

Read-only fresh scene revision remained `1.4048400085633866752.5.254` before/after these observations/render; view revision settled at `1.4048400085633866752.16.3`. Public request IDs and tokens are historical evidence, not reusable inputs for another session. No live editing or camera/device-switch tests were performed.

## Inspection/request construction: verified facts and limit

Live server tools/list declares **only request_id and scene_id required for vsr_inspect**. All filters/paging fields are optional. Current rejected live inspection payloads included object_type/object_subtype withkindnodes, placeholder zero handles and empty optional filters, reproducing invalid_request without operation admission. These are malformed/mis-targeted caller inputs; they do not demonstrate a broken node/edit backend. My trials repeated this over-filled shape and are not presented as successful minimal live Queen_B queries.

Read-only installed Pi source reconnaissance found MCP inputSchema requiredness preserved and no normal constrainedSampling strict rewrite on MCP ToolDefinitions. This gives **no evidence that standard Pi forces every filter**. It does not instrument the actual external/custom model-facing gateway; absence of a Pi source defect is not exhaustive proof of all possible upstream transformations. Nor does the rendered function signature alone prove native requiredness. Do not relax server kind checks or introduce blanket empty-string stripping on that basis.

Existing native MCP protocol regressions executed successfully on the current built test executable:
- `Scenario: MCP node queries parse filters and expose placement records`:1case/97assertions;
- `Scenario: MCP node enabled edit inspect restore uses only wire tokens`:1case/133assertions.

Those exercise omission-based valid node queries and versioned edit/inspection/restoration on isolated fixtures, not the running chess Scene. **Remaining live gap:** a correctly minimal Queen_B/pawn query and live inspect/version/reinspect workflow were not completed in this validation. No live vsr_edit occurred. Testing malformed all-field payloads repeatedly adds no evidence; a native omission-preserving request path should be used for that last live check without stealing/replacing the existing controller.

The visible scene tree shows pawn top/body nesting; any future rearrangement must inspect actual ancestry and square coordinates. Blindly applying the same local delta to a parent and its child can double the child's displacement. Move the common piece parent where appropriate, or compute atomic final world positions with fresh ancestry versions.

## Proposed improvements, ordered

### 1. Make each registered view truthful immediately

Expose stable view identity/name and an explicit role such as interactive_viewport or independent_agent. Wire effective cached host binding into vsr_view_info for both real viewport and legacy agent view, preserving safe-point ownership and bounded representations. Report actual Device Identifier, bound camera/renderer, render-buffer versus display dimensions, applicable final Image Pipeline settings and source revisions; unknowns retain explicit reasons. Discovery and render results should carry enough provenance to prevent an agent from calling an independent frame a viewport screenshot. Keep legacyrender behavior and its permissions; unexposed newcapture policy does not mean legacyrender must be disabled.

This overlaps existing camera-capture14(read-only real/agent binding observation), with20's policy integration prerequisite. Reuse those contracts instead of duplicating them. Metadata does not enable capture or qualify a backend.

### 2. Add explicit presented-frame semantics

For 'show what I am seeing', retrieve the latest completed **presented viewport frame**, not a new Helide rendering. A deep host-owned Frame Observation module should expose matched frame metadata and bounded final output through one owning-thread seam. Bind frame identity, timestamp, scene/view/configuration revisions actually rendered, Device Identifier, camera/renderer and final Image Pipeline/color conversion/scale. Current Scene revisions can be newer than the cached frame: distinguish historical displayed output from freshly rendered content.

Differentiate pipeline image from UI overlays/window screenshot and native render-buffer dimensions from display scaling. The observed1005x217buffer is displayed at1340x290; returning either without that distinction is misleading. No new render/camera/device mutation merely to fetch an observation. CPU-resident cached output may be reusable; GPU-only readback must still be asynchronously qualified/bounded or report unavailable. No silent agent-view fallback.

Existing08(selected-viewport capture) owns a **fresh capture using a snapshot**, not automatically last-presented-frame retrieval. Keep separate semantics even if they share the adapter/retention/permission machinery. Current limits remain unchanged; unsupported/oversized output fails or has explicitly requested/documented scaling, never a hidden cap increase.

### 3. Make valid inspection requests easier than malformed ones

Keep native omission semantics. Add short kind-specific minimal examples and prominently state:omit unused filters; empty strings can mean exact empty-name filters; handles must come from fresh inspection, not placeholders; filters intersect. Return bounded structured argument diagnostics(field,kind,reason) rather than onlyinvalid_request, without leaking Scene/private data.

Consider narrower object/layer/node tools or a compatible kind-discriminated schema while retaining existingvsr_inspect. Choose against actual provider schema support; do not assume all clients support structuredoneOf. Fix optional-field handling only where an actual omission-losing layer is demonstrated. Additional-properties/schema consistency can be considered without changing accepted meaningful empty values or weakening kind-specific rejection.

### 4. Qualification and regression evidence

Automate omitted/minimal filters, meaningful empty filters, bad mixed-kind filters, placeholder/foreign handles, pre-admission rejection/request-ID behavior, and inspect→fresh-version→atomic edit→reinspection→restore through the real client seam. Live smoke uses the running chess Scene only with explicit edit/restore permission and fresh versions, not offline coordinates or historical tokens.

For viewport work, test view selection, camera manipulation, Device Identifier/renderer switch, resize/render scale, pipeline effects, closure/recreation, stale frame/current-revision distinction, multiple viewports, permission withdrawal, cancellation and bounded retention/readback. Then compare a real displayed viewport with the returned frame; mock-only tests do not establish parity. Preserve heartbeat/nonperturbation/CPU-CUDA qualification contracts and do not claim samples from unverified render-call counts.

## Practical conclusion

Frame mismatch is confirmed on the live application, not a stale installed executable explanation. Missing view/bounds binding is a known host-wiring limitation. Server-side node breakage and standard Pi schema-forcing are not established; caller construction is demonstrably wrong, native minimal/edited protocol fixtures are green, and the legal live-query path remains to be checked. Priority is truthful viewport/agent identity and a presented-frame interface, with request construction/diagnostics as a separate smaller improvement. No source implementation or new ticket/spec was published by this validation.
