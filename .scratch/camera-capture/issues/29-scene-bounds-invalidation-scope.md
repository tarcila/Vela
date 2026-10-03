# 29 — Authorize scene-owned bounds invalidation repair

**Kind:** extends
**Status:** resolved
**Milestone:** 1
**Blocked by:** none
**Specification:** [Camera & Capture](../spec.md), Implementation Decision 3; Testing Decision 2.
**Files:** src/vsr/scene/LayerNodeData.hpp, src/vsr/scene/LayerNodeData.cpp, src/vsr/scene/Layer.hpp, src/vsr/scene/Layer.cpp, src/vsr/scene/Scene.cpp, src/vsr/core/Forest.hpp, src/vsr/integration/identity/Identity.hpp, src/tests/integration/test_IntegrationIdentity.cpp

## Decision needed

Authorize a separately scoped scene/core repair establishing conservative, bounded whole-scene bounds dependency validity for public node payload and hierarchy mutations. These scene/core files are outside ticket 28's allowed Files. Ticket 28 explicitly requires blocking on a hole when required notification repair is outside scope; no scene/core source was changed.

This extends implementation scope, not the frozen bounds contract. The spec's invalidation requirement remains correct. No assumption that every caller emits manual notifications is adopted. Do not resolve by automatically stamping supplied boxes current, weakening mutation coverage, requiring ready rendering, or adding an unbounded query-time scene fingerprint.

## Confirmed behavior

`SceneIdentity::Observer` advances scene/inspection revisions only when notified. `LayerNodeData::noteEdit()` updates per-payload history without emitting a scene notification; that local history cannot validate a complete scene box without finding every contributor. `Layer::erase()` only calls the Forest erase. `Scene::insertChildNode()` inserts without the structure signal emitted by the transform/object insertion helpers. Public Forest insertion/membership APIs also do not notify the Scene.

A diagnostic against the current public headless API with a real Scene confirmed six mutation classes leave **both** scene and inspection tokens unchanged. An explicit host signal control advances both. This could keep a previously supplied complete world box current after local/ancestor transforms, visibility, removal or membership changes. Existing node-edit/ancestry token tests do not prove complete scene validity.

### Reproduction

Create a real `Scene`, `IdentityAuthority` and `SceneIdentity`; obtain `scene.defaultLayer()`. Insert a parent and child with `scene.insertChildTransformNode(..., vsr::math::IDENTITY_MAT4)`. For each operation below snapshot `ids.sceneRevision()` and `ids.inspectionRevision()`, mutate, then compare both tokens:

1. `child->value().setAsTransform(vsr::math::translation_matrix(vsr::math::float3(7.f, 2.f, 1.f)))`.
2. Parent transform translated by `(4,0,0)` and changed back to identity.
3. `parent->value().setEnabled(false)`.
4. `layer->erase(child)`.
5. `scene.insertChildNode(parent, "new membership")`.
6. `parent->insert_last_child(vsr::scene::LayerNodeData(layer, vsr::math::IDENTITY_MAT4, "direct"))`.
7. Control: `scene.signalLayerTransformChanged(layer)`.

Observed for cases 1–6: `scene_changed=0 inspection_changed=0`; control: both 1. Diagnostic expected both tokens to change on each mutation and exited 1 with `missing invalidation cases=6 of 7`. Case 5 is empty membership (inspection must change); cases 1–4 and 6 include correctness-bearing bounds dependencies. The diagnostic source/output remain ignored under `.scratch/camera-capture-logs/28-notification-probe.{cpp,log}`; the reproduction above is durable, not dependent on those files.

## Required follow-through after authorization

- Establish a scene-owned notification/revision mechanism covering actual public payload and topology mutations, including local/ancestor transforms, enablement, object-reference membership, insert/erase/reparent/copy/remove/reuse and change-back. Audit instance parameters as part of the same coverage; their setters currently do not call `noteEdit()` either.
- Preserve existing precise node edit/ancestor history, object lifetime and batching/restoration semantics. Do not invalidate or dereference moved/constructing payloads unsafely; blindly notifying from every constructor/destructor is not a qualified fix.
- Test through public APIs with real Scenes, not only explicit `signalLayer*Changed()` controls. Bound safe-point work without retaining growing per-query identity history or walking the whole scene for a supplied value.
- Rerun full enabled integration/broad-editing regressions with fresh build/install. Then resume 28: all four supplied value states, validation/storage/authority and MCP coverage are still unimplemented. This hole alone is not completion of 28.

## Comments

### 2026-10-03 iteration — ready-for-human
Filed during ticket 28 orientation, before implementation, on confirming out-of-scope dependency validity repairs. No spec/addenda/sibling/source/user configuration/artifact changes; no bounds value publication or device work.
Verified: `cmake --build _out/_cmake -j 4` and `cmake --install _out/_cmake` → passed with the existing CUDA/UI/network/Lua/SDL3/MCP enabled configuration, no configure/fallback.
Verified: diagnostic compilation reused the `test_IntegrationIdentity.cpp` entry from `_out/_cmake/compile_commands.json`; linking reused `ninja -C _out/_cmake -t commands vsrIntegrationTests` with only the input object/output executable replaced. `.scratch/camera-capture-logs/28-notification-probe` → exit 1, six missing-invalidation cases and one positive signal control (intentional red evidence, not implementation success).
Verified: `_out/_cmake/vsrIntegrationTests '[bounds],[bounds-mcp]'` → 8 cases passed; `_out/_cmake/vsrIntegrationTests '[integration]'` → 276 cases passed. These qualify only the unchanged baseline, not current supplied bounds.
Holes filed: none (this is the hole).

### 2026-10-03 human decision — resolved
User authorized the separately scoped scene/core dependency-validity repair in34, including all actual public payload/topology/instance-parameter mutations. Preserve existing manual render/UI callback delivery, batching, precise node/ancestor histories, lifetimes and generic Forest/DataTree semantics. Automatic invalidation does not authorize automatic coarse callbacks, whole-scene fingerprint walks or arbitrary mutable epoch pointers.28 waits for implementation34 (or its complete replacement chain). This resolves authorization only; the six reproduced holes remain unfixed until positive regression qualification.
