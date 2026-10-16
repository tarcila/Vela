// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/content/AllocationProfile.hpp"
#include "vsr/integration/content/PreparedHdriPublication.hpp"
#include "vsr/integration/content/detail/PreparedSurfaceGraph.hpp"
#include "vsr/integration/operations/Reservation.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedSurfaceContent.hpp"

namespace vsr::integration::content {

struct SurfaceHandles
{
  identity::ObjectHandle geometry, material, surface;
  std::array<identity::ObjectHandle, 64> arrays{};
  size_t arrayCount{0};
  identity::LayerHandle layer;
  identity::NodeHandle contentRoot, surfaceNode;
};

/* Unregistered structural publication. This does not admit a request or grant
 * processing/retention capacity. All identities and receipt bytes are prepared
 * before commit, enrolled before callbacks, and abandoned serials are never
 * reused. No source metadata, filename, label or old target selects identity.
 */
class PreparedSurfacePublication
{
 public:
  PreparedSurfacePublication(identity::SceneIdentity &identity,
      scene::LayerNodeRef parent,
      const detail::PreparedSurfaceGraph &graph,
      const operations::Reservation &reserve,
      scene::Token newLayerName = {});
  VSR_NOT_COPYABLE(PreparedSurfacePublication)
  VSR_NOT_MOVEABLE(PreparedSurfacePublication)
  static bool profileStorage(identity::SceneIdentity &identity,
      scene::LayerNodeRef parent,
      const detail::PreparedSurfaceGraph &graph,
      bool newLayer,
      core::StorageProfile &profile);
  const SurfaceHandles &handles() const;
  const std::string &inventoryReceipt() const;
  bool isCurrent() const;
  bool committed() const;
  bool commit();
  void notify();

 private:
  static std::shared_ptr<const void> reservePreparation(
      identity::SceneIdentity &identity,
      scene::LayerNodeRef parent,
      const detail::PreparedSurfaceGraph &graph,
      bool newLayer,
      const operations::Reservation &reserve);
  // Declared first: all private plans die before their processing lease.
  std::shared_ptr<const void> m_preparation;
  identity::SceneIdentity *m_identity{nullptr};
  std::weak_ptr<const void> m_registration;
  scene::detail::PreparedSurfaceContent m_transaction;
  SurfaceHandles m_handles;
  std::string m_receipt;
  using SerialNode = decltype(identity::SceneIdentity::m_serialOf)::node_type;
  using ObjectNode = decltype(identity::SceneIdentity::m_entries)::node_type;
  using LayerSerialNode =
      decltype(identity::SceneIdentity::m_layerSerialOf)::node_type;
  using LayerNode = decltype(identity::SceneIdentity::m_layers)::node_type;
  using RootNode = decltype(identity::SceneIdentity::m_rootLayerOf)::node_type;
  using NodeSerialNode =
      decltype(identity::SceneIdentity::m_nodeSerialOf)::node_type;
  using NodeNode = decltype(identity::SceneIdentity::m_nodes)::node_type;
  std::array<SerialNode, 67> m_serials;
  std::array<ObjectNode, 67> m_objects;
  LayerSerialNode m_layerSerial;
  LayerNode m_layer;
  RootNode m_layerRoot;
  std::array<NodeSerialNode, 2> m_nodeSerials;
  std::array<NodeNode, 2> m_nodes;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool PreparedSurfacePublication::profileStorage(
    identity::SceneIdentity &ids,
    scene::LayerNodeRef parent,
    const detail::PreparedSurfaceGraph &graph,
    bool newLayer,
    core::StorageProfile &profile)
{
  const auto count = graph.scene().numberOfObjects(ANARI_ARRAY) + 3;
  if (count > 67 || !ids.scene()
      || !scene::detail::PreparedSurfaceContent::profileStorage(*ids.scene(),
          parent,
          graph.geometry(),
          graph.material(),
          newLayer,
          profile)
      || ids.m_nodeSerialOf.size() > 32766)
    return false;
  return PreparedHdriPublication::profileRegistry(
             ids.m_serialOf, count, profile)
      && PreparedHdriPublication::profileRegistry(ids.m_entries, count, profile)
      && PreparedHdriPublication::profileRegistry(ids.m_nodes, 2, profile)
      && PreparedHdriPublication::profileRegistry(
          ids.m_layerSerialOf, newLayer ? 1 : 0, profile)
      && PreparedHdriPublication::profileRegistry(
          ids.m_layers, newLayer ? 1 : 0, profile)
      && PreparedHdriPublication::profileRegistry(
          ids.m_rootLayerOf, newLayer ? 1 : 0, profile)
      && profile.add(ids.m_nodeSerialOf.size() + 4,
          sizeof(decltype(ids.m_nodeSerialOf)::value_type) + 4 * sizeof(void *))
      // Receipt string growth and final capacity coexist during preparation.
      && profile.add(3 * 8192 + sizeof(PreparedSurfacePublication));
}

inline std::shared_ptr<const void>
PreparedSurfacePublication::reservePreparation(identity::SceneIdentity &ids,
    scene::LayerNodeRef parent,
    const detail::PreparedSurfaceGraph &graph,
    bool newLayer,
    const operations::Reservation &reserve)
{
  core::StorageProfile profile(64u << 20);
  if (!allocation_profile::AVAILABLE
      || !profileStorage(ids, parent, graph, newLayer, profile))
    throw std::runtime_error("Surface allocation profile unavailable");
  auto lease = reserve(profile.bytes());
  if (!lease)
    throw std::bad_alloc();
  return lease;
}

inline PreparedSurfacePublication::PreparedSurfacePublication(
    identity::SceneIdentity &ids,
    scene::LayerNodeRef parent,
    const detail::PreparedSurfaceGraph &graph,
    const operations::Reservation &reserve,
    scene::Token newLayerName)
    : m_preparation(reservePreparation(ids,
          parent,
          graph,
          bool(newLayerName) || !ids.scene()->numberOfLayers(),
          reserve)),
      m_identity(&ids),
      m_registration(ids.lifetime()),
      m_transaction(*ids.scene(),
          parent,
          graph.geometry(),
          graph.material(),
          graph.root()->value(),
          newLayerName)
{
  auto &t = m_transaction;
  m_handles.arrayCount = t.m_arrays.size();
  decltype(ids.m_serialOf) serials;
  decltype(ids.m_entries) objects;
  for (size_t i = 0; i < m_handles.arrayCount + 3; ++i) {
    const scene::Object *o = nullptr;
    size_t index = 0;
    identity::ObjectHandle *handle = nullptr;
    if (i < m_handles.arrayCount) {
      o = &t.m_arrays[i];
      index = t.m_arrayIndex + i;
      handle = &m_handles.arrays[i];
    } else if (i == m_handles.arrayCount) {
      o = &t.m_geometry;
      index = t.m_geometryIndex;
      handle = &m_handles.geometry;
    } else if (i == m_handles.arrayCount + 1) {
      o = &t.m_material;
      index = t.m_materialIndex;
      handle = &m_handles.material;
    } else {
      o = &t.m_surface;
      index = t.m_surfaceIndex;
      handle = &m_handles.surface;
    }
    const identity::SceneIdentity::Key key{ids.normalize(o->type()), index};
    if (ids.m_serialOf.count(key))
      throw std::runtime_error("Surface slot already registered");
    const auto serial = ids.m_authority->nextSerial();
    serials.emplace(key, serial);
    identity::SceneIdentity::Entry entry{
        key, 1, ids.fingerprint(*o), {}, i < m_handles.arrayCount, true};
    if (i < m_handles.arrayCount)
      entry.arrayLifetime = t.m_arrays[i].lifetime();
    objects.emplace(serial, std::move(entry));
    m_serials[i] = serials.extract(key);
    m_objects[i] = objects.extract(serial);
    *handle = {ids.m_authority->launch(), ids.m_domain, serial};
  }
  ids.m_serialOf.reserve(ids.m_serialOf.size() + m_handles.arrayCount + 3);
  ids.m_entries.reserve(ids.m_entries.size() + m_handles.arrayCount + 3);
  auto *layer = t.m_layer.lock()->layer;
  if (t.m_newLayer) {
    const auto serial = ids.m_authority->nextSerial();
    const auto rootSerial = ids.m_authority->nextSerial();
    decltype(ids.m_layerSerialOf) ls;
    decltype(ids.m_layers) layers;
    decltype(ids.m_rootLayerOf) roots;
    ls.emplace(layer->lifetime(), serial);
    layers.emplace(serial,
        identity::SceneIdentity::LayerEntry{
            layer, layer->lifetime(), layer->root()->lifetime(), rootSerial});
    roots.emplace(rootSerial, serial);
    m_layerSerial = ls.extract(layer->lifetime());
    m_layer = layers.extract(serial);
    m_layerRoot = roots.extract(rootSerial);
    ids.m_layerSerialOf.reserve(ids.m_layerSerialOf.size() + 1);
    ids.m_layers.reserve(ids.m_layers.size() + 1);
    ids.m_rootLayerOf.reserve(ids.m_rootLayerOf.size() + 1);
    m_handles.layer = {ids.m_authority->launch(), ids.m_layerDomain, serial};
  } else {
    m_handles.layer = ids.handleOf(layer);
    if (!m_handles.layer.valid())
      throw std::runtime_error("Unregistered Surface Layer");
  }
  decltype(ids.m_nodeSerialOf) ns;
  decltype(ids.m_nodes) nodes;
  identity::NodeHandle *handles[] = {
      &m_handles.contentRoot, &m_handles.surfaceNode};
  for (size_t i = 0; i < 2; ++i) {
    const auto serial = ids.m_authority->nextSerial();
    const auto key = std::make_pair(m_handles.layer.serial, t.m_rootLife + i);
    ns.emplace(key, serial);
    nodes.emplace(serial,
        identity::SceneIdentity::NodeEntry{
            m_handles.layer.serial, t.m_rootLife + i, t.m_extent + i});
    m_nodeSerials[i] = ns.extract(key);
    m_nodes[i] = nodes.extract(serial);
    *handles[i] = {ids.m_authority->launch(), ids.m_nodeDomain, serial};
  }
  ids.m_nodes.reserve(ids.m_nodes.size() + 2);
  const auto wire = [](const auto &h) {
    return PreparedHdriPublication::wireHandle(h.domain, h.serial);
  };
  m_receipt = "{\"surface\":\"" + wire(m_handles.surface) + "\",\"geometry\":\""
      + wire(m_handles.geometry) + "\",\"material\":\""
      + wire(m_handles.material) + "\",\"content_root\":\""
      + wire(m_handles.contentRoot) + "\",\"layer\":\"" + wire(m_handles.layer)
      + "\",\"surface_node\":\"" + wire(m_handles.surfaceNode)
      + "\",\"borrowed\":[],\"arrays\":[";
  for (size_t i = 0; i < m_handles.arrayCount; ++i) {
    if (i)
      m_receipt += ',';
    m_receipt += '"' + wire(m_handles.arrays[i]) + '"';
  }
  m_receipt += "]}";
  if (m_receipt.size() > 8192)
    throw std::runtime_error("Surface inventory exceeds bound");
}

inline const SurfaceHandles &PreparedSurfacePublication::handles() const
{
  return m_handles;
}
inline const std::string &PreparedSurfacePublication::inventoryReceipt() const
{
  return m_receipt;
}
inline bool PreparedSurfacePublication::committed() const
{
  return m_transaction.committed();
}
inline bool PreparedSurfacePublication::isCurrent() const
{
  if (m_registration.expired() || !m_transaction.isCurrent())
    return false;
  const auto &ids = *m_identity;
  const auto count = m_handles.arrayCount + 3;
  if (!PreparedHdriPublication::fits(ids.m_serialOf, count)
      || !PreparedHdriPublication::fits(ids.m_entries, count)
      || !PreparedHdriPublication::fits(ids.m_nodes, 2))
    return false;
  for (size_t i = 0; i < count; ++i)
    if (ids.m_serialOf.count(m_serials[i].key())
        || ids.m_entries.count(m_objects[i].key()))
      return false;
  for (size_t i = 0; i < 2; ++i)
    if (ids.m_nodeSerialOf.count(m_nodeSerials[i].key())
        || ids.m_nodes.count(m_nodes[i].key()))
      return false;
  if (!m_layer.empty()) {
    if (!PreparedHdriPublication::fits(ids.m_layerSerialOf, 1)
        || !PreparedHdriPublication::fits(ids.m_layers, 1)
        || !PreparedHdriPublication::fits(ids.m_rootLayerOf, 1)
        || ids.m_layerSerialOf.count(m_layerSerial.key())
        || ids.m_layers.count(m_layer.key())
        || ids.m_rootLayerOf.count(m_layerRoot.key()))
      return false;
  } else if (!ids.resolve(m_handles.layer))
    return false;
  return true;
}
inline bool PreparedSurfacePublication::commit()
{
  if (!isCurrent() || !m_transaction.commit())
    return false;
  auto &ids = *m_identity;
  for (size_t i = 0; i < m_handles.arrayCount + 3; ++i) {
    ids.m_serialOf.insert(std::move(m_serials[i]));
    ids.m_entries.insert(std::move(m_objects[i]));
    ids.advanceRevision(ids.m_sceneRevision);
    ids.inspectionChanged();
  }
  for (size_t i = 0; i < 2; ++i) {
    ids.m_nodeSerialOf.insert(std::move(m_nodeSerials[i]));
    ids.m_nodes.insert(std::move(m_nodes[i]));
  }
  if (!m_layer.empty()) {
    ids.m_layerSerialOf.insert(std::move(m_layerSerial));
    ids.m_layers.insert(std::move(m_layer));
    ids.m_rootLayerOf.insert(std::move(m_layerRoot));
  }
  return true;
}
inline void PreparedSurfacePublication::notify()
{
  m_transaction.notify();
}

} // namespace vsr::integration::content
