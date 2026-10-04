# Handoff: viewport capture mismatch and blocked chess-piece rearrangement

## Goal / user expectation

The user asked the agent to look at the current VSR scene and describe what is being rendered. They expected the image as rendered in the interactive viewport, including its camera, selected ANARI Device Identifier, renderer configuration, dimensions, and Image Pipeline effects—not an independent rendering of the same scene.

After the first capture, the user said: “You should have been getting the frame as rendered in the viewport, there it seems you rendered it with Helide.” A second capture reproduced the same-looking image. They requested this handoff for an implementer to review and improve the behavior.

Scope of this session: live observations, limited source reconnaissance, and this document only. No implementation, build, tests, scene edits, or resynchronization were performed.

## Workspace / existing artifacts

Workspace: `/home/tarcila/Code/ANARI/vela/broad-editing`.

Follow `AGENTS.md`, `CODING_STANDARDS.md`, and relevant domain/ADR guidance. Use Jujutsu, not raw git. Keep handoffs/specs/tickets under `.scratch/`; do not publish without explicit permission.

At handoff preparation, `jj status` showed existing local changes to `.envrc` and `.pi/mcp.json`; do not overwrite these. Working-copy revision was `uururqzt` / `ee11c31a`, parent `nsvssrwr` / `05c6f1b2` (`broad-editing*`, “chore(tickets): record restored cumulative gate”). These are a snapshot, not a requirement to reset the workspace.

Existing related design/work artifacts (read instead of duplicating their contracts):
- `.scratch/camera-capture/spec.md`
- `.scratch/camera-capture/proposal.md`
- `.scratch/camera-capture/issues/`
- `.scratch/camera-capture/finish-review.md`, `repair-review.md`, `repair-run.md`
- `.scratch/vsr-mcp-gaps.md`
- `.scratch/broad-editing/`

The camera-capture spec already explicitly identifies the separate fixed-camera Helide agent view and forbids silently presenting agent-device images as screenshots of another viewport. Review its implementation status before proposing overlapping work. Immutable one-off capture and obtaining the current viewport frame are related but not identical requirements.

## Live observations (facts)

1. `vsr_discover` returned scene 0 and view 0. View 0 advertised `read=true`, `render=true`; capture policy was `exposed=false`, `reason=unqualified_binding`, with no quality units.
2. Scene inspection succeeded. The scene hierarchy includes `ABeautifulGame`, a chessboard, and a full black/white chess set in its initial arrangement. Objects include 15 triangle geometries, 15 physicallyBased materials, 84 image2D samplers, and one HDRI light named `footprint_court.hdr`.
3. `vsr_view_info(view_id=0)` returned:
   - `state=unavailable`, `reason=missing_binding`
   - `scene_revision=1.11539072420779787194.5.254`
   - `view_revision=1.11539072420779787194.16.3`
   - `capture_policy_revision=1.11539072420779787194.20.1`
   - capture policy still `exposed=false`, `reason=unqualified_binding`.
4. `vsr_bounds(scene_id=0)` also returned `state=unavailable`, `reason=missing_binding`. This does not mean empty geometry.
5. `vsr_render` with those scene/view revisions nevertheless completed with `outcome=ok`. `vsr_render_image` returned a 640×360 PNG, `capture=1`.
6. A repeat view-info query still returned the same missing binding and tokens. A second render succeeded with `capture=2`, again 640×360 and visually the same image. No byte-for-byte comparison was performed.
7. Both images showed the chessboard from the white side, surrounded by black background, with muted stone-like foreground pieces and green/gold accents on the distant pieces. These are observations of the agent render ONLY; they do not establish any defect in viewport materials, HDRI visibility, or textures.

Session request sequence: discover 1; objects 2; nodes 3; view info 4; bounds 5; render 6; repeat view info 7; repeat render 8. Revision tokens/request IDs are historical and must not be reused blindly in a new session. Query fresh state and use fresh IDs.

## Follow-up: chess-piece rearrangement did not execute

### Requested outcome

The user next asked to put White in checkmate and chose to rearrange pieces rather than require a single legal move from the initial position. The proposed arrangement was Fool’s Mate, corresponding to `1. f3 e5 2. g4 Qh4#`:
- White f-pawn: f2 → f3.
- White g-pawn: g2 → g4.
- Black e-pawn: e7 → e5.
- Black queen: d8 → h4.
- Leave the other pieces in their starting positions.

The imported pawns have separate body/top transform nodes. Both parts of each moved pawn must be moved consistently. Determine square coordinates from freshly inspected transforms rather than assuming file orientation or pawn numbering.

### What happened

All follow-up `vsr_inspect` calls (request IDs 9–19, eleven requests) returned only `invalid_request`, without an operation reference or detailed explanation. No fresh node edit versions were obtained. **No `vsr_edit` call was made; no pieces were moved by the agent, and no rearrangement was verified.** Earlier unfiltered node inspection (request 3) had succeeded.

Representative rejected request (request 9; request 18 used the same filters):

```json
{
  "request_id": 9,
  "scene_id": 0,
  "page_size": 64,
  "cursor": "",
  "kind": "nodes",
  "name": "Queen_B",
  "name_contains": "",
  "object_subtype": "",
  "handle": "000000000000000f0000000000000108",
  "object_type": "ANARI_SURFACE",
  "node_kind": "transform",
  "layer_handle": "000000000000000a0000000000000100",
  "parent_handle": "000000000000000f0000000000000103"
}
```

### Diagnosis caveat: malformed agent requests, not a proven server defect

The assistant told the user that the node-inspection tool needed fixing. That conclusion was premature: the submitted requests included **object-only filters** (`object_type`, `object_subtype`) with `kind=nodes`, contrary to the exposed inspection contract. They also supplied empty optional filter/cursor strings instead of omitting them. The pawn queries additionally filtered `handle` to the `ABeautifulGame` parent node while searching for pawn children; intersecting those filters would not select the intended pawns even if the request were admitted.

No minimal, correctly filtered follow-up query was tested. Repeated near-identical invalid requests added no diagnostic evidence. Do not label this a scene-editing backend failure or relax validation to accept invalid mixed-kind filters on that basis.

### Implementer follow-up and verification

1. Reproduce with a minimal legal node query, for example `{request_id: <fresh>, scene_id: 0, kind: "nodes", name: "Queen_B"}`, omitting unrelated optional fields. Separately query pawn transform nodes with `name_contains="Pawn_"` and `node_kind="transform"`, without object-only filters or a parent-node target handle.
2. Check the MCP tool schema as exported by the server and as presented by the client/harness: can irrelevant fields actually be omitted, or does an adapter incorrectly make all properties required? The observed bloated payloads are grounds to investigate, not proof of a schema conversion defect.
3. Keep strict kind-specific filter validation. Consider actionable invalid-request diagnostics and clearer schema/help examples so an agent can identify the rejected field rather than guessing repeatedly.
4. Inspect fresh names, positions, `node_edit_version`, and `structural_version`; use `ancestry_version` as well if choosing world-space position edits. Preserve the documented version and atomic edit contracts.
5. Resolve the three pawn identities from board coordinates, move their body/top nodes together, and move the black queen. Prefer one atomic edit batch where supported. Do not reuse historical node versions from this handoff.
6. Reinspect all changed transforms and verify the Fool’s Mate arrangement. When viewport capture is available, obtain a matching frame as visual evidence; do not substitute the independent Helide render without labeling it.
7. Add regression coverage for valid minimal node filters, rejection of object-only filters on node queries, optional-field handling through the MCP client boundary, and an inspect → versioned edit → reinspection workflow. Include a live scene-edit smoke test.

This follow-up adds a second review topic: tool discoverability/request construction and the scene-edit workflow, separate from the viewport capture mismatch. There is still no implementation or test result to claim.

## Source reconnaissance (confirmed locally)

`src/apps/interactive/viewer/vsrViewer.cpp`:
- Around lines 54–59, the comment explicitly describes `AgentView` as a Helide device with its own offscreen Image Pipeline, independent of interactive viewports, and its own framing camera.
- `AgentView::WIDTH=640`, `HEIGHT=360` (around lines 61–62).
- `AgentView::setup()` loads `"helide"` and acquires its RenderIndex (around lines 106–109), creates its own perspective camera, and builds its own pipeline.
- `Application::setupIntegration()` (around line 622) creates this AgentView; scene sync/recovery hooks target it.
- Around lines 654–658, registration names the view `"agent"` and grants read/render.
- Around lines 697–703, RenderService attaches that AgentView backend and sets the fixed dimensions.
- Inspector construction starts around line 706; examine the remaining setup for missing host binding/observer wiring.

Other useful starting points:
- `src/vsr/integration/mcp/tools/RenderTools.hpp`: legacy revision-pinned `vsr_render` and retained PNG delivery via `vsr_render_image`.
- `src/vsr/integration/mcp/tools/QueryTools.hpp`: discover/view-info/bounds tool contract.
- `src/vsr/integration/render/RevisionRender.hpp`: backend registration, render admission, dimensions, completion.
- `src/vsr/integration/inspection/ViewInfo.hpp`: effective binding validation/provenance and unqualified capture policy.
- `src/vsr/integration/inspection/Inspection.hpp`: missing-binding handling (bounds around line 1545; view info around line 1618).
- `src/vsr/integration/Integration.hpp` / `.cpp`: view registration and lifecycle.
- `src/vsr/integration/mcp/README.md`: current API contracts, including existing legacy render behavior.
- `src/tests/integration/test_IntegrationMcpTools.cpp` and `test_IntegrationInspection.cpp`: existing missing-binding cases.

Line numbers are navigation hints, not stable identifiers.

## Interpretation / avoid overstating

The inspected source confirms a deliberate separate Helide backend, not merely a guessed fallback based on image appearance. Whether the running executable exactly matches this checkout has not been verified. Establish build/runtime provenance before declaring the exact runtime cause closed.

The missing-binding response and successful legacy render may be permitted by existing separate API contracts. Do not assume `capture_policy.exposed=false` must automatically disable legacy `vsr_render`: review the spec and compatibility obligations first. The actionable mismatch is that the agent was offered a renderable view without usable provenance and treated it as the user's viewport.

The assistant initially described the returned image as though it represented the viewport and speculated about material/environment discrepancies. It later corrected that claim. Improve both host/tool truthfulness and agent-facing guidance; adding metadata alone does not make the agent view the viewport.

## Requested implementer review

1. Establish which executable/build hosts the MCP connection and reproduce with fresh discover/view-info/render calls.
2. Trace the interactive viewport's device, camera, renderer, dimensions, and final Image Pipeline output against the registered agent view. Identify the appropriate lifecycle/safe-point integration seam.
3. Decide how to expose the actual requested view: retrieving its displayed frame versus rendering a fresh matching frame. Make that semantic distinction explicit; do not silently substitute an offscreen Helide image.
4. Review existing camera-capture tickets and contracts before modifying render/capture admission or adding endpoints. Preserve intentional headless/legacy behavior, but clearly label it as independent where retained.
5. Supply usable provenance and clear discoverability for each view. When no viewport binding exists, report that limitation rather than implying viewport parity. Unsupported devices/pipelines should fail explicitly or be honestly qualified.
6. Cover camera manipulation, device switching, renderer changes, resize, pipeline effects, viewport closure/recreation, stale revisions, and multi-viewport selection. Preserve safe-point scheduling, permissions, cleanup, cancellation, and bounded image retention.

## Acceptance evidence

- On this chess scene, identify the chosen interactive viewport and return a frame whose camera/composition, backend appearance, and pipeline effects match it, with dimensions and provenance explicitly reported (or explicitly documented scaling).
- Move the viewport camera, switch device, change renderer settings, and resize: fresh observations/captures track the chosen viewport rather than staying at the fixed agent camera/640×360 Helide setup.
- Missing/unsupported bindings never produce an image falsely labeled as viewport output.
- If independent agent rendering remains supported, its identity and differences are explicit and independently testable.
- Add automated regression coverage at the host binding/render-tool seam, then validate on a real interactive viewport. Mock-only tests do not demonstrate visual parity.
- Run the repository's authoritative build/test commands for the affected configuration; record commands and outcomes. Do not claim verification until run.

## Suggested skills

Load skills from `~/.pi/agent/skills/<name>/SKILL.md` before use:
- `diagnosing-bugs`: establish a reproducible feedback loop and distinguish host wiring from intended legacy behavior.
- `codebase-design`: select a deep host/viewport adapter seam without leaking UI machinery into integration core.
- `tdd`: regression cases for provenance, selection, lifecycle, and render semantics.
- `code-review`: final standards/spec review against the applicable existing artifacts.

This handoff is evidence and a review request, not authorization to publish issues, discard local changes, or bypass existing capture permissions.
