// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/PreparedHdriPublication.hpp"
#include "vsr/integration/content/ScopedUsage.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedHdriRemoval.hpp"
// std
#include <array>
#include <optional>

namespace vsr::integration::content {

/* Fixed metadata-only exact-removal evidence. Allocate its owning record before
 * publication, then seal without allocation before callbacks. It does not pin
 * radiance, Light, Layer, Scene, or pooled references. A removal plan
 * temporarily owns tombstones; their processing charge is separate from this
 * record. This is an internal primitive, not public admission or a second undo
 * store.
 */
struct HdriRemovalEvidence
{
  bool seal(identity::SceneIdentity &identity,
      const HdriHandles &handles,
      bool ownsLayer);
  const char *check(identity::SceneIdentity &identity) const;

  HdriHandles handles;
  std::weak_ptr<scene::detail::SceneDependencyState> scene;
  std::weak_ptr<scene::detail::LayerOwnerState> layer;
  std::weak_ptr<const void> registration;
  std::array<scene::ObjectVersion, 2> parameters{};
  std::array<scene::ObjectVersion, 2> metadata{};
  std::array<std::uint64_t, 3> edits{};
  std::array<std::uint64_t, 3> labels{};
  std::array<std::uint64_t, 3> topology{};
  std::uint64_t parentLifetime{0};
  std::uint64_t dataWrite{0};
  std::array<size_t, 3> dimensions{};
  std::uint64_t layerMetadata{0};
  bool ownsLayer{false};
  bool sealed{false};
};

class PreparedHdriRemoval
{
 public:
  PreparedHdriRemoval(
      identity::SceneIdentity &identity, const HdriRemovalEvidence &evidence);
  VSR_NOT_COPYABLE(PreparedHdriRemoval)
  VSR_NOT_MOVEABLE(PreparedHdriRemoval)
  bool isCurrent() const;
  bool committed() const;
  bool commit();
  void notify();

 private:
  identity::SceneIdentity *m_identity{nullptr};
  HdriRemovalEvidence m_evidence;
  std::unique_ptr<scene::detail::PreparedHdriRemoval> m_transaction;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool HdriRemovalEvidence::seal(identity::SceneIdentity &identity,
    const HdriHandles &published,
    bool ownLayer)
{
  if (sealed)
    return false;
  auto *s = identity.scene();
  auto *l = identity.resolve(published.layer);
  auto *light = identity.resolve(published.light);
  auto *array =
      dynamic_cast<scene::Array *>(identity.resolve(published.radiance));
  auto root = identity.resolve(published.contentRoot);
  auto node = identity.resolve(published.lightNode);
  if (!s || !l || !light || !array || !root || !node
      || !array->dataWriteSequence())
    return false;
  handles = published;
  scene = scene::detail::PreparedHdriRemoval::sceneLifetime(*s);
  layer = scene::detail::PreparedHdriRemoval::layerLifetime(*l);
  registration = identity.lifetime();
  ownsLayer = ownLayer;
  const scene::Object *objects[] = {array, light};
  for (size_t i = 0; i < 2; ++i) {
    parameters[i] = objects[i]->lastParameterChange();
    metadata[i] = objects[i]->lastMetadataChange();
  }
  const scene::LayerNodeRef nodes[] = {root, node, l->root()};
  for (size_t i = 0; i < (ownLayer ? 3u : 2u); ++i) {
    edits[i] = nodes[i]->value().editVersion();
    labels[i] = nodes[i]->value().metadataSequence();
    topology[i] = nodes[i]->topologyVersion();
  }
  parentLifetime = root->parent()->lifetime();
  dataWrite = *array->dataWriteSequence();
  for (size_t i = 0; i < 3; ++i)
    dimensions[i] = array->dim(i);
  layerMetadata = ownLayer ? l->metadataSequence() : 0;
  sealed = true;
  return check(identity) == nullptr;
}

inline const char *HdriRemovalEvidence::check(
    identity::SceneIdentity &identity) const
{
  const auto s = scene.lock();
  const auto owner = layer.lock();
  if (!sealed || registration.expired() || !s || !s->scene || !owner
      || !owner->layer || !owner->dependencyAttached
      || identity.lifetime().lock() != registration.lock()
      || identity.scene() != s->scene)
    return "owner_retired";
  const auto writes = s->scene->imageWriteSequence();
  if (!writes || !writes->isEnabled() || !writes->isOwningThread())
    return "write_contract_unavailable";
  auto *l = identity.resolve(handles.layer);
  auto *light = identity.resolve(handles.light);
  auto *array =
      dynamic_cast<scene::Array *>(identity.resolve(handles.radiance));
  auto root = identity.resolve(handles.contentRoot);
  auto node = identity.resolve(handles.lightNode);
  if (!l || l != owner->layer || !light || !array || !root || !node)
    return "owned_identity_changed";
  if (array->isMapped())
    return "array_mapped";
  if (!array->isHost() || array->elementType() != ANARI_FLOAT32_VEC3
      || array->type() != ANARI_ARRAY2D || !array->dataWriteSequence()
      || *array->dataWriteSequence() != dataWrite)
    return "radiance_changed";
  for (size_t i = 0; i < 3; ++i)
    if (array->dim(i) != dimensions[i])
      return "radiance_changed";
  const scene::Object *objects[] = {array, light};
  for (size_t i = 0; i < 2; ++i)
    if (objects[i]->lastParameterChange() != parameters[i]
        || objects[i]->lastMetadataChange() != metadata[i])
      return "owned_object_changed";
  const scene::LayerNodeRef nodes[] = {root, node, l->root()};
  for (size_t i = 0; i < (ownsLayer ? 3u : 2u); ++i)
    if (nodes[i]->value().editVersion() != edits[i]
        || nodes[i]->value().metadataSequence() != labels[i]
        || nodes[i]->topologyVersion() != topology[i])
      return "owned_node_changed";
  if (!root->parent() || root->parent()->lifetime() != parentLifetime
      || root->next() != node || node->parent() != root || !node->isLeaf()
      || node->sibling() != root)
    return "owned_membership_changed";
  if (ownsLayer
      && (l->size() != 3 || root->parent() != l->root()
          || l->metadataSequence() != layerMetadata))
    return "owned_layer_changed";
  // Use counts include disabled bindings, app/animation/internal pins and all
  // ordinary Layer placements. Only the closure's Light -> radiance and single
  // placement are safe. No borrowed values are part of this evidence.
  if (array->totalUseCount() != 1
      || array->useCount(scene::Object::UseKind::PARAMETER) != 1
      || light->totalUseCount() != 1
      || light->useCount(scene::Object::UseKind::LAYER) != 1
      || node->value().getObject() != light
      || light->parameterValueAsObject("radiance") != array)
    return "outside_use";
  return scopedUsage(*s->scene, light, array, node);
}

inline PreparedHdriRemoval::PreparedHdriRemoval(
    identity::SceneIdentity &identity, const HdriRemovalEvidence &evidence)
    : m_identity(&identity), m_evidence(evidence)
{
  if (evidence.check(identity))
    throw std::runtime_error("Conflicting HDRI removal evidence");
  m_transaction =
      std::make_unique<scene::detail::PreparedHdriRemoval>(*identity.scene(),
          identity.resolve(evidence.handles.contentRoot),
          identity.resolve(evidence.handles.lightNode),
          identity.resolve(evidence.handles.light)->index(),
          identity.resolve(evidence.handles.radiance)->index(),
          evidence.ownsLayer);
}

inline bool PreparedHdriRemoval::isCurrent() const
{
  return !m_evidence.registration.expired() && !m_evidence.check(*m_identity)
      && m_transaction->isCurrent();
}

inline bool PreparedHdriRemoval::committed() const
{
  return m_transaction->committed();
}

inline bool PreparedHdriRemoval::commit()
{
  if (!isCurrent() || !m_transaction->commit())
    return false;
  // Unregister before any consumer, including one that throws, can observe the
  // complete deletion. Callback tombstones cannot transfer old handle
  // authority.
  m_identity->untrack(m_transaction->removedLight());
  m_identity->untrack(m_transaction->removedArray());
  if (m_transaction->removedLayer())
    m_identity->untrackLayer(m_transaction->removedLayer());
  return true;
}

inline void PreparedHdriRemoval::notify()
{
  m_transaction->notify();
}

} // namespace vsr::integration::content
