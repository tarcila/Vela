# Addendum 04 — Identity signals, inspection item shape, discovery admission and wire revision tokens

**Extends:** spec §6, §7, §9 and §12. **Source:** holes 25, 55, 115, 116, 117, 118. **Decided by:** orchestrator.

## Decisions
1. **Unsignalled changes (25, 55.3):** name and metadata changes must bump the inspection revision without an O(objects) refresh on every inspection slice. Add a minimal metadata/name change signal to `vsr::scene::Object`, or an equivalent incremental mechanism. Animation-definition and layer-name changes remain outside the scene revision for v1. Cameras and renderers remain view state.
2. **Inspection items (55.1):** besides handle, type, subtype, parameter version, name and parameter name/type pairs, inspection items carry **current values** for the §8 editable parameters of matte materials and directional lights. Values are serialized as JSON numbers (the binary64 of the stored float32), and direction as `[azimuth, elevation]` in degrees. Other parameter values remain omitted.
3. **Cursor rejection (55.2):** tampered, foreign-session, foreign-query and foreign-target cursors are rejected before admission with `invalid_request` (see addendum 03).
4. **Removal crash (55.4):** the segfault seen when inspecting after mass object removal must be diagnosed and fixed. Inspection after arbitrary removal must be safe.
5. **Discovery admission (115):** discovery is admitted against a session-wide target and never fails only because the first scene is unreadable. The retained record's covered targets are exactly the targets listed on that page. Discovery with zero readable targets returns an empty completed page.
6. **Edit result detail (116):** edit and restore step results carry `batch_ref` (edit), `committed`, `scene_revision`, and the affected objects' new `versions`. On value rejection they carry `failed_entry` and `value_error`. These are finer structured reasons, which §9 allows.
7. **Expired reason (117):** a restore of an expired batch still returns `outcome_unavailable`, with the result `{reason:"expired"}`. Unknown and wrong-launch batches carry no reason.
8. **Wire revision tokens (118):** inspection page results carry `scene_revision`. Discovery view items carry `view_revision`. Edit and restore results carry `scene_revision`. All use the render module's existing string form, `"<activation>.<nonce>.<domain>.<value>"`. With these, an agent can form its first render request from inspection alone.

## Why
The spec requires that "Inspection returns scene/view tokens; a committed edit returns its resulting scene token" (§6/§12), and that a discovery page covers the targets it lists (§9). The first useful workflow (§Problem Statement, §14 gate 5) needs values and tokens to be observable over the wire.
