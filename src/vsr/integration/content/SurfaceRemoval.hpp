// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/PreparedSurfacePublication.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedSurfaceRemoval.hpp"

namespace vsr::integration::content {

/* Fixed, metadata-only evidence. No payload, pooled pointer, or strong Scene/
 * Layer/Array reference. Tombstone processing ownership is separate; this is
 * not yet a shared-history adapter or an admitted public Restore operation. */
struct SurfaceRemovalEvidence
{
  bool seal(identity::SceneIdentity &identity,
      const SurfaceHandles &published,
      bool ownLayer);
  const char *check(identity::SceneIdentity &identity) const;
  SurfaceHandles handles;
  std::weak_ptr<scene::detail::SceneDependencyState> scene;
  std::weak_ptr<scene::detail::LayerOwnerState> layer;
  std::weak_ptr<const void> registration;
  std::array<scene::ObjectVersion, 67> parameters{}, metadata{};
  std::array<std::uint64_t, 64> writes{};
  std::array<size_t, 64> uses{}, elements{};
  std::array<anari::DataType, 64> types{};
  std::array<std::uint64_t, 3> edits{}, labels{}, topology{}, children{};
  std::uint64_t parentLife{0}, layerMetadata{0};
  bool ownsLayer{false}, sealed{false};
};

class PreparedSurfaceRemoval
{
 public:
  PreparedSurfaceRemoval(identity::SceneIdentity &identity,
      const SurfaceRemovalEvidence &evidence,
      const operations::Reservation &reserve);
  VSR_NOT_COPYABLE(PreparedSurfaceRemoval)
  VSR_NOT_MOVEABLE(PreparedSurfaceRemoval)
  bool isCurrent() const;
  bool committed() const;
  bool commit();
  void notify();

 private:
  std::shared_ptr<const void> m_preparation;
  identity::SceneIdentity *m_identity{nullptr};
  SurfaceRemovalEvidence m_evidence;
  std::unique_ptr<scene::detail::PreparedSurfaceRemoval> m_transaction;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool SurfaceRemovalEvidence::seal(identity::SceneIdentity &ids,
    const SurfaceHandles &published,
    bool ownLayer)
{
  if (sealed || !published.arrayCount || published.arrayCount > 64)
    return false;
  handles = published;
  ownsLayer = ownLayer;
  auto *s = ids.scene();
  auto *l = ids.resolve(handles.layer);
  auto root = ids.resolve(handles.contentRoot);
  auto node = ids.resolve(handles.surfaceNode);
  auto *g = ids.resolve(handles.geometry);
  if (!s || !l || !root || !node || !g || !root->parent())
    return false;
  scene = scene::detail::PreparedHdriRemoval::sceneLifetime(*s);
  layer = scene::detail::PreparedHdriRemoval::layerLifetime(*l);
  registration = ids.lifetime();
  for (size_t i = 0; i < handles.arrayCount + 3; ++i) {
    const auto h = i < handles.arrayCount ? handles.arrays[i]
        : i == handles.arrayCount         ? handles.geometry
        : i == handles.arrayCount + 1     ? handles.material
                                          : handles.surface;
    const auto *o = ids.resolve(h);
    if (!o)
      return false;
    parameters[i] = o->lastParameterChange();
    metadata[i] = o->lastMetadataChange();
    if (i < handles.arrayCount) {
      const auto *a = dynamic_cast<const scene::Array *>(o);
      if (!a || !a->dataWriteSequence())
        return false;
      writes[i] = *a->dataWriteSequence();
      elements[i] = a->size();
      types[i] = a->elementType();
      for (size_t p = 0; p < g->numParameters(); ++p)
        uses[i] += g->parameterValueAsObject(g->parameterNameAt(p)) == a;
    }
  }
  const scene::LayerNodeRef nodes[] = {root, node, l->root()};
  for (size_t i = 0; i < (ownsLayer ? 3u : 2u); ++i) {
    edits[i] = nodes[i]->value().editVersion();
    labels[i] = nodes[i]->value().metadataSequence();
    topology[i] = nodes[i]->topologyVersion();
    children[i] = nodes[i]->childrenVersion();
  }
  parentLife = root->parent()->lifetime();
  layerMetadata = ownsLayer ? l->metadataSequence() : 0;
  sealed = true;
  return check(ids) == nullptr;
}

inline const char *SurfaceRemovalEvidence::check(
    identity::SceneIdentity &ids) const
{
  const auto s = scene.lock();
  const auto owner = layer.lock();
  if (!sealed || registration.expired() || !s || !s->scene || !owner
      || !owner->layer || !owner->dependencyAttached
      || ids.lifetime().lock() != registration.lock()
      || ids.scene() != s->scene)
    return "owner_retired";
  const auto sequence = s->scene->imageWriteSequence();
  if (!sequence || !sequence->isEnabled() || !sequence->isOwningThread())
    return "write_contract_unavailable";
  auto *l = ids.resolve(handles.layer);
  auto root = ids.resolve(handles.contentRoot);
  auto node = ids.resolve(handles.surfaceNode);
  const auto *g = ids.resolve(handles.geometry);
  const auto *m = ids.resolve(handles.material);
  const auto *surface = ids.resolve(handles.surface);
  if (!l || l != owner->layer || !root || !node || !g || !m || !surface)
    return "owned_identity_changed";
  std::array<const scene::Object *, 67> objects{};
  for (size_t i = 0; i < handles.arrayCount + 3; ++i) {
    const auto *o = i < handles.arrayCount ? ids.resolve(handles.arrays[i])
        : i == handles.arrayCount          ? g
        : i == handles.arrayCount + 1      ? m
                                           : surface;
    if (!o)
      return "owned_identity_changed";
    objects[i] = o;
    if (o->lastParameterChange() != parameters[i]
        || o->lastMetadataChange() != metadata[i])
      return "owned_object_changed";
    if (i < handles.arrayCount) {
      const auto *a = dynamic_cast<const scene::Array *>(o);
      if (!a || !a->isHost() || a->isMapped() || a->type() != ANARI_ARRAY1D
          || a->elementType() != types[i] || a->size() != elements[i]
          || !a->dataWriteSequence() || *a->dataWriteSequence() != writes[i])
        return "owned_array_changed";
      if (a->totalUseCount() != uses[i]
          || a->useCount(scene::Object::UseKind::PARAMETER) != uses[i])
        return "outside_use";
    } else if (o->totalUseCount() != 1
        || o->useCount(i == handles.arrayCount + 2
                   ? scene::Object::UseKind::LAYER
                   : scene::Object::UseKind::PARAMETER)
            != 1)
      return "outside_use";
  }
  const scene::LayerNodeRef nodes[] = {root, node, l->root()};
  for (size_t i = 0; i < (ownsLayer ? 3u : 2u); ++i)
    if (nodes[i]->value().editVersion() != edits[i]
        || nodes[i]->value().metadataSequence() != labels[i]
        || nodes[i]->topologyVersion() != topology[i]
        || nodes[i]->childrenVersion() != children[i])
      return "owned_node_changed";
  if (!root->parent() || root->parent()->lifetime() != parentLife
      || root->next() != node || node->parent() != root || !node->isLeaf()
      || node->sibling() != root || node->value().getObject() != surface)
    return "owned_membership_changed";
  if (ownsLayer
      && (l->size() != 3 || root->parent() != l->root()
          || l->metadataSequence() != layerMetadata))
    return "owned_layer_changed";
  // Counts alone miss raw Any bindings, defaults, min/max and object Arrays.
  // Scan every supported reference location with a finite work bound.
  size_t slots = 0, values = 0;
  const char *failure = nullptr;
  const auto owned = [&](const core::Any &v) {
    const auto *o = v.holdsObject() ? s->scene->getObject(v) : nullptr;
    if (!o)
      return false;
    for (size_t i = 0; i < handles.arrayCount + 3; ++i)
      if (objects[i] == o)
        return true;
    return false;
  };
  const auto scan = [&](const auto &pool) {
    if (failure)
      return;
    if (pool.capacity() > 32768 - slots) {
      failure = "reference_scan_limit";
      return;
    }
    slots += pool.capacity();
    for (size_t i = 0; i < pool.capacity() && !failure; ++i) {
      const auto o = pool.at(i);
      if (!o)
        continue;
      if (const auto *a = dynamic_cast<const scene::Array *>(o.data());
          a && anari::isObject(a->elementType())) {
        if (a->size() > 131072 - values || !a->isHost() || a->isMapped()
            || (a->size() && !a->data())) {
          failure = "reference_scan_limit";
          break;
        }
        values += a->size();
        for (size_t j = 0; j < a->size(); ++j)
          if (owned(core::Any(a->elementType(),
                  static_cast<const size_t *>(a->data())[j]))) {
            failure = "outside_use";
            break;
          }
      }
      if (o->numParameters() > (131072 - values) / 3) {
        failure = "reference_scan_limit";
        break;
      }
      values += o->numParameters() * 3;
      for (size_t p = 0; p < o->numParameters(); ++p) {
        const auto &parameter = o->parameterAt(p);
        const auto *target = s->scene->getObject(parameter.value());
        const auto name = std::string_view(o->parameterNameAt(p));
        const bool internal =
            (o.data() == g && target && target->type() == ANARI_ARRAY1D)
            || (o.data() == surface
                && ((name == "geometry" && target == g)
                    || (name == "material" && target == m)));
        if (owned(parameter.min()) || owned(parameter.max())
            || (owned(parameter.value()) && !internal)) {
          failure = "outside_use";
          break;
        }
      }
    }
  };
  const auto &db = s->scene->objectDB();
  scan(db.array);
  scan(db.geometry);
  scan(db.material);
  scan(db.surface);
  scan(db.sampler);
  scan(db.volume);
  scan(db.field);
  scan(db.light);
  scan(db.camera);
  scan(db.renderer);
  for (const auto &entry : s->scene->layers()) {
    const auto *layer = entry.second.ptr.get();
    if (failure)
      break;
    if (layer->capacity() > 32768 - slots)
      return "reference_scan_limit";
    slots += layer->capacity();
    for (size_t i = 0; i < layer->capacity(); ++i) {
      const auto n = layer->at(i);
      if (!n)
        continue;
      const auto &p = n->value().getInstanceParameters();
      if (131072 - values < 2 || p.size() > 131072 - values - 2)
        return "reference_scan_limit";
      values += 2 + p.size();
      if ((owned(n->value().valueRaw()) && n != node)
          || owned(n->value().defaultValueRaw()))
        return "outside_use";
      for (const auto &v : p)
        if (owned(v.second))
          return "outside_use";
    }
  }
  return failure;
}

inline PreparedSurfaceRemoval::PreparedSurfaceRemoval(
    identity::SceneIdentity &ids,
    const SurfaceRemovalEvidence &evidence,
    const operations::Reservation &reserve)
    : m_identity(&ids), m_evidence(evidence)
{
  if (evidence.check(ids))
    throw std::runtime_error("Conflicting Surface removal evidence");
  core::StorageProfile profile(64u << 20);
  const auto *geometry = dynamic_cast<const scene::Geometry *>(
      ids.resolve(evidence.handles.geometry));
  const auto *material = dynamic_cast<const scene::Material *>(
      ids.resolve(evidence.handles.material));
  // Same bounded pools/Forest and metadata backing as adoption, with additional
  // empty replacements and tombstone-vector storage. Payload is transferred,
  // not copied into undo; it stays covered while tombstones own it.
  if (!allocation_profile::AVAILABLE || !geometry || !material
      || !scene::detail::PreparedSurfaceContent::profileStorage(*ids.scene(),
          ids.resolve(evidence.handles.contentRoot),
          *geometry,
          *material,
          false,
          profile)
      || !profile.add(evidence.handles.arrayCount,
          2 * sizeof(scene::Array) + sizeof(scene::ArrayLifetime)
              + 4 * sizeof(void *))
      || !profile.add(sizeof(PreparedSurfaceRemoval)
          + sizeof(scene::detail::PreparedSurfaceRemoval)))
    throw std::runtime_error("Surface removal allocation profile unavailable");
  m_preparation = reserve(profile.bytes());
  if (!m_preparation)
    throw std::bad_alloc();
  std::array<size_t, 64> arrays{};
  for (size_t i = 0; i < evidence.handles.arrayCount; ++i)
    arrays[i] = ids.resolve(evidence.handles.arrays[i])->index();
  m_transaction =
      std::make_unique<scene::detail::PreparedSurfaceRemoval>(*ids.scene(),
          ids.resolve(evidence.handles.contentRoot),
          ids.resolve(evidence.handles.surfaceNode),
          ids.resolve(evidence.handles.geometry)->index(),
          ids.resolve(evidence.handles.material)->index(),
          ids.resolve(evidence.handles.surface)->index(),
          arrays,
          evidence.handles.arrayCount,
          evidence.ownsLayer);
}
inline bool PreparedSurfaceRemoval::isCurrent() const
{
  return !m_evidence.registration.expired() && !m_evidence.check(*m_identity)
      && m_transaction->isCurrent();
}
inline bool PreparedSurfaceRemoval::committed() const
{
  return m_transaction->committed();
}
inline bool PreparedSurfaceRemoval::commit()
{
  if (!isCurrent() || !m_transaction->commit())
    return false;
  for (size_t i = 0; i < m_evidence.handles.arrayCount + 3; ++i)
    m_identity->untrack(m_transaction->removed(i));
  // Retire the two lifetime-map entries now, not lazily in a later lookup.
  for (const auto h :
      {m_evidence.handles.contentRoot, m_evidence.handles.surfaceNode}) {
    const auto it = m_identity->m_nodes.find(h.serial);
    if (it != m_identity->m_nodes.end()) {
      m_identity->m_nodeSerialOf.erase(
          {it->second.layerSerial, it->second.lifetime});
      m_identity->m_nodes.erase(it);
    }
  }
  if (m_transaction->removedLayer())
    m_identity->untrackLayer(m_transaction->removedLayer());
  return true;
}
inline void PreparedSurfaceRemoval::notify()
{
  m_transaction->notify();
}

} // namespace vsr::integration::content
