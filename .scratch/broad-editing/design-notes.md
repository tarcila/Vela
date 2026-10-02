# Broad editing: design discussion

Status: design discussion synthesized into [spec.md](spec.md), labeled
`ready-for-agent`. This file records decisions/background; the spec is the
implementation contract.

## Motivation and acceptance direction

Source: `.scratch/vsr-mcp-gaps.md` and the two chess-session Lua workarounds.
The goal is to make live scene rearrangement possible through structured VSR
MCP operations without rebuilding the scene offline or manually executing Lua.
The first acceptance example is Fool's Mate: identify the existing placements,
move the four affected pieces as one batch, inspect the changed scene, and
restore the batch. Camera/capture and image-file export are a later increment.

## User-approved decisions

1. First increment focuses on live rearrangement, not the entire queen's-eye
   screenshot workflow.
2. Use structured operations, not an arbitrary live Lua execution capability.
3. Reading is broader than editing: expose layer/node hierarchy, names, enabled
   state, local/world transforms, object references, ordinary scalar/vector/
   string parameter values, and filtering by type/name. Arrays are summaries,
   not bulk contents. Advertise paging limits.
4. Existing-node edits include position, rotation, scale, full transform matrix,
   and enabled state. Creation, deletion, reparenting, and reference replacement
   are out of scope.
5. Local space is canonical; additionally support world-space positioning.
6. Preserve atomic batches, stale-state rejection, normal scene notifications,
   and conditional restoration. The four chess moves should be one restorable
   batch.
7. Rotation convenience edits use VSR's existing degrees-based azimuth/
   elevation/roll convention, with explicit fields and documented axes/order.
8. Position-only edits preserve the existing matrix's linear part. Reject
   rotation/scale convenience edits when faithful SRT representation is not
   possible; allow explicit full-matrix editing instead. Transform edits target
   existing transform nodes; enabled-state edits can target other node kinds.
   Do not implicitly create transform wrappers.
9. Edit conflicts cover the targeted node and relevant ancestry, not every
   change in the layer. World-space positioning must detect parent-transform
   changes. Restoration must not overwrite intervening human edits and should
   not be blocked by unrelated placement changes.

The user approved the scope, inspection/edit surface, and three transform/
conflict recommendations with successive "Looks good" responses. These are
design approvals; code implementation has not been requested yet.

## Facts checked in the current code

- Inspection includes values only for the existing five editable matte/
  directional-light fields. Other parameter entries expose name/type.
- Inspection defaults to 64 items and allows at most 256; tool schema currently
  advertises only a minimum, not the maximum.
- Layer nodes can contain transforms, object references, empty values, or
  transform arrays. A shared scene object is not the same as a placement.
- A node stores its full local matrix alongside a cached scale/rotation/
  translation representation. SRT editing reconstructs the matrix.
- Existing rotation controls use degrees and VSR's azimuth/elevation/roll
  convention; this is not an unspecified generic XYZ Euler convention.
- Node setters do not signal scene updates themselves; callers use the scene's
  layer notifications. Existing identity notification hooks name a layer, not
  an individual node; there are no integration node edit handles/versions yet.
- Viewer agent rendering has a separate offscreen helide camera; merely editing
  a stored scene camera will not necessarily change the rendered agent view.

## Technical details completed in the specification

The core feature direction is settled. The specification makes these details
explicit without expanding the approved edit surface:

- Filtering syntax, parameter-value encoding, and bounded inspection behavior.
- Identity lifetime across node deletion/reuse and structural changes, including
  relevant ancestry tokens for world-space positioning.
- Transform validation, matrix layout, singular parents, and the numerical
  tolerance for faithful SRT representation.
- Mixed placement/object-parameter batch behavior and restoration of parent/
  child edits in one batch.
- Transform-array nodes are not individual scalar transforms; clarify summary
  inspection and rejection of unsupported transform edits.

The specification synthesis uses the already confirmed public headless test
seam; it does not introduce another test interface. No independent spec review,
implementation plan, ticket graph, or code implementation has been produced
for this increment.

## Explicitly deferred

- Shared result-store refactor and FlatMap migration (integration ticket 226).
- Live Lua execution, camera/view editing, one-off capture settings, image-file
  export, broader material/light/renderer parameter editing.
- Node creation/deletion/reparenting or changing an object's placement reference.
- Publishing local working notes or tickets.
