// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/identity/Identity.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedHdriContent.hpp"
// std
#include <array>
#include <cstdio>
#include <string>

namespace vsr::integration::content {

struct HdriHandles
{
  identity::ObjectHandle radiance;
  identity::ObjectHandle light;
  identity::LayerHandle layer;
  identity::NodeHandle contentRoot;
  identity::NodeHandle lightNode;
};

/* Internal safe-point publication, NOT a public creation operation. Map nodes
 * and handle inventory are detached until commit; abandoned preparation burns
 * serials but exposes no identity or revision. All registry bucket growth is
 * before Scene writes. The caller still owes source/authority, shared recovery,
 * full terminal receipt/ownership and processing-budget reservations.
 *
 * No pooled pointer or ref is retained. The registration lifetime guards the
 * non-owning identity pointer; the Scene transaction guards its own lifetimes.
 * Publication contains no callbacks between the final check and registry adopt.
 */
class PreparedHdriPublication
{
 public:
  PreparedHdriPublication(identity::SceneIdentity &identity,
      scene::LayerNodeRef parent,
      const char *rootName,
      size_t width,
      size_t height,
      const math::float3 *radiance,
      const scene::detail::HdriSettings &settings = {},
      scene::Token newLayerName = {});
  VSR_NOT_COPYABLE(PreparedHdriPublication)
  VSR_NOT_MOVEABLE(PreparedHdriPublication)

  static bool profileStorage(identity::SceneIdentity &identity,
      scene::LayerNodeRef parent,
      size_t pixels,
      bool newLayer,
      core::StorageProfile &profile);
  static bool profileInitial(identity::SceneIdentity &identity,
      scene::LayerNodeRef parent,
      bool newLayer,
      size_t ancestry,
      core::StorageProfile &profile);
  size_t liveBackingBytes() const;
  bool isCurrent() const;
  bool committed() const;
  const HdriHandles &handles() const;
  const std::string &inventoryReceipt() const;
  bool commit();
  void notify();

 private:
  using SerialNode = decltype(identity::SceneIdentity::m_serialOf)::node_type;
  using ObjectNode = decltype(identity::SceneIdentity::m_entries)::node_type;
  using LayerSerialNode =
      decltype(identity::SceneIdentity::m_layerSerialOf)::node_type;
  using LayerNode = decltype(identity::SceneIdentity::m_layers)::node_type;
  using RootNode = decltype(identity::SceneIdentity::m_rootLayerOf)::node_type;
  using NodeSerialNode =
      decltype(identity::SceneIdentity::m_nodeSerialOf)::node_type;
  using NodeNode = decltype(identity::SceneIdentity::m_nodes)::node_type;

  template <typename Map>
  static bool fits(const Map &map, size_t additions);
  template <typename Map>
  static bool profileRegistry(
      const Map &map, size_t additions, core::StorageProfile &profile);
  static std::string wireHandle(std::uint64_t domain, std::uint64_t serial);
  void prepareObjects();
  void prepareHierarchy();

  identity::SceneIdentity *m_identity{nullptr};
  std::weak_ptr<const void> m_registration;
  scene::detail::PreparedHdriContent m_transaction;
  HdriHandles m_handles;
  std::string m_receipt;
  std::array<SerialNode, 2> m_serials;
  std::array<ObjectNode, 2> m_objects;
  LayerSerialNode m_layerSerial;
  LayerNode m_layer;
  RootNode m_layerRoot;
  std::array<NodeSerialNode, 2> m_nodeSerials;
  std::array<NodeNode, 2> m_nodes;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename Map>
inline bool PreparedHdriPublication::profileRegistry(
    const Map &map, size_t additions, core::StorageProfile &profile)
{
#ifdef __GLIBCXX__
  if (map.size() > 32768 - additions || map.bucket_count() > 65536
      || map.max_load_factor() != 1.f)
    return false;
  std::__detail::_Prime_rehash_policy policy(map.max_load_factor());
  const auto next =
      policy._M_next_bkt(policy._M_bkt_for_elements(map.size() + additions));
  // libstdc++ nodes contain value + next pointer + optional cached hash,
  // with alignment padding. Four pointer words conservatively cover both
  // node variants. Each temporary map's initial insertion uses 13 buckets.
  return next <= 65536
      && profile.add(map.size() + 2 * additions,
          sizeof(typename Map::value_type) + 4 * sizeof(void *))
      && profile.add(map.bucket_count() + next + 13, sizeof(void *));
#else
  return false;
#endif
}

inline bool PreparedHdriPublication::profileStorage(
    identity::SceneIdentity &ids,
    scene::LayerNodeRef parent,
    size_t pixels,
    bool newLayer,
    core::StorageProfile &profile)
{
  if (!ids.scene()
      || !scene::detail::PreparedHdriContent::profileStorage(
          *ids.scene(), parent, pixels, newLayer, profile)
      || ids.m_nodeSerialOf.size() > 32766)
    return false;
  // Ordered node-lifetime map: value, three links, colour/padding. Extracted
  // node handles reuse allocations; no metadata fingerprint owns a snapshot.
  return profile.add(ids.m_nodeSerialOf.size() + 4,
             sizeof(decltype(ids.m_nodeSerialOf)::value_type)
                 + 4 * sizeof(void *))
      && profileRegistry(ids.m_serialOf, 2, profile)
      && profileRegistry(ids.m_entries, 2, profile)
      && profileRegistry(ids.m_nodes, 2, profile)
      && profileRegistry(ids.m_layerSerialOf, newLayer ? 1 : 0, profile)
      && profileRegistry(ids.m_layers, newLayer ? 1 : 0, profile)
      && profileRegistry(ids.m_rootLayerOf, newLayer ? 1 : 0, profile)
      && profile.add(sizeof(PreparedHdriPublication) + 2 * 513);
}

inline bool PreparedHdriPublication::profileInitial(
    identity::SceneIdentity &ids,
    scene::LayerNodeRef parent,
    bool newLayer,
    size_t ancestry,
    core::StorageProfile &profile)
{
  if (ancestry > 1024 || ids.m_nodeSerialOf.size() > 32768 - ancestry
      || !profileStorage(ids, parent, 0, newLayer, profile))
    return false;
  // handleOf lazily creates both lifetime and reverse entries. Profile before
  // the first lookup, not retroactively after the worker. Rehash can double
  // the previous bucket count, independently of the next-prime element bound.
  size_t buckets = 0;
#ifdef __GLIBCXX__
  std::__detail::_Prime_rehash_policy policy(1.f);
  buckets = policy._M_next_bkt(std::max(size_t(13),
      std::max(2 * ids.m_nodes.bucket_count(),
          2 * (ids.m_nodes.size() + ancestry))));
  if (buckets > 65536)
    return false;
#else
  return false;
#endif
  return profileRegistry(ids.m_nodes, ancestry, profile)
      && profile.add(2 * buckets, sizeof(void *))
      && profile.add(ancestry,
          sizeof(decltype(ids.m_nodeSerialOf)::value_type) + 4 * sizeof(void *))
      && profile.add(ancestry,
          sizeof(
              std::pair<identity::NodeHandle, identity::NodeEditVersionToken>)
              + sizeof(std::uint64_t));
}

inline size_t PreparedHdriPublication::liveBackingBytes() const
{
  // A conservative requested-backing profile of now-live storage, NOT bytes
  // reclaimed. This remains measurable even if preparation is abandoned.
  core::StorageProfile profile(64u << 20);
  const auto &ids = *m_identity;
  const auto layer = m_transaction.m_layer.lock();
  if (!layer || !layer->layer)
    return 0;
  const auto *scene = ids.scene();
  scene->objectDB().array.profileStorage(profile, 0);
  scene->objectDB().light.profileStorage(profile, 0);
  layer->layer->profileStorage(profile, 0);
  const auto buckets = [&](const auto &map) {
    profile.add(map.bucket_count(), sizeof(void *));
    profile.add(map.size(),
        sizeof(typename std::decay_t<decltype(map)>::value_type)
            + 4 * sizeof(void *));
  };
  buckets(ids.m_serialOf);
  buckets(ids.m_entries);
  buckets(ids.m_nodes);
  buckets(ids.m_layerSerialOf);
  buckets(ids.m_layers);
  buckets(ids.m_rootLayerOf);
  profile.add(ids.m_nodeSerialOf.size(),
      sizeof(decltype(ids.m_nodeSerialOf)::value_type) + 4 * sizeof(void *));
  profile.add(m_transaction.m_radiance.size(), sizeof(core::math::float3));
  return profile.valid() ? profile.bytes() : 0;
}

template <typename Map>
inline bool PreparedHdriPublication::fits(const Map &map, size_t additions)
{
  return additions <= map.max_size() - map.size()
      && static_cast<long double>(map.size() + additions)
      <= static_cast<long double>(map.bucket_count()) * map.max_load_factor();
}

inline std::string PreparedHdriPublication::wireHandle(
    std::uint64_t domain, std::uint64_t serial)
{
  char text[33];
  std::snprintf(text,
      sizeof(text),
      "%016llx%016llx",
      (unsigned long long)domain,
      (unsigned long long)serial);
  return text;
}

inline PreparedHdriPublication::PreparedHdriPublication(
    identity::SceneIdentity &identity,
    scene::LayerNodeRef parent,
    const char *rootName,
    size_t width,
    size_t height,
    const math::float3 *radiance,
    const scene::detail::HdriSettings &settings,
    scene::Token newLayerName)
    : m_identity(&identity),
      m_registration(identity.lifetime()),
      m_transaction(*identity.scene(),
          parent,
          rootName,
          width,
          height,
          radiance,
          settings,
          newLayerName)
{
  prepareObjects();
  prepareHierarchy();
  const auto object = [](const identity::ObjectHandle &h) {
    return wireHandle(h.domain, h.serial);
  };
  const auto node = [](const identity::NodeHandle &h) {
    return wireHandle(h.domain, h.serial);
  };
  m_receipt = "{\"radiance\":\"" + object(m_handles.radiance)
      + "\",\"light\":\"" + object(m_handles.light) + "\",\"layer\":\""
      + wireHandle(m_handles.layer.domain, m_handles.layer.serial)
      + "\",\"content_root\":\"" + node(m_handles.contentRoot)
      + "\",\"light_node\":\"" + node(m_handles.lightNode) + "\"}";
  // This fixed two-object/two-node inventory never grows at publication.
  if (m_receipt.size() > 512)
    throw std::runtime_error("HDRI inventory receipt exceeds bounds");
}

inline void PreparedHdriPublication::prepareObjects()
{
  auto &ids = *m_identity;
  decltype(ids.m_serialOf) serials;
  decltype(ids.m_entries) objects;
  const scene::Object *prepared[] = {
      &m_transaction.m_radiance, &m_transaction.m_light};
  const size_t indices[] = {
      m_transaction.m_arrayIndex, m_transaction.m_lightIndex};
  identity::ObjectHandle *handles[] = {&m_handles.radiance, &m_handles.light};
  for (size_t i = 0; i < 2; ++i) {
    const identity::SceneIdentity::Key key{
        ids.normalize(prepared[i]->type()), indices[i]};
    if (ids.m_serialOf.count(key))
      throw std::runtime_error("HDRI object slot already registered");
    const auto serial = ids.m_authority->nextSerial();
    serials.emplace(key, serial);
    identity::SceneIdentity::Entry entry{
        key, 1, ids.fingerprint(*prepared[i]), {}, i == 0, true};
    if (i == 0)
      entry.arrayLifetime = m_transaction.m_radiance.lifetime();
    objects.emplace(serial, std::move(entry));
    m_serials[i] = serials.extract(key);
    m_objects[i] = objects.extract(serial);
    *handles[i] = {ids.m_authority->launch(), ids.m_domain, serial};
  }
  ids.m_serialOf.reserve(ids.m_serialOf.size() + 2);
  ids.m_entries.reserve(ids.m_entries.size() + 2);
}

inline void PreparedHdriPublication::prepareHierarchy()
{
  auto &ids = *m_identity;
  auto *layer = m_transaction.m_layer.lock()->layer;
  if (m_transaction.m_newLayer) {
    const auto serial = ids.m_authority->nextSerial();
    const auto rootSerial = ids.m_authority->nextSerial();
    decltype(ids.m_layerSerialOf) serials;
    decltype(ids.m_layers) layers;
    decltype(ids.m_rootLayerOf) roots;
    serials.emplace(layer->lifetime(), serial);
    layers.emplace(serial,
        identity::SceneIdentity::LayerEntry{
            layer, layer->lifetime(), layer->root()->lifetime(), rootSerial});
    roots.emplace(rootSerial, serial);
    m_layerSerial = serials.extract(layer->lifetime());
    m_layer = layers.extract(serial);
    m_layerRoot = roots.extract(rootSerial);
    ids.m_layerSerialOf.reserve(ids.m_layerSerialOf.size() + 1);
    ids.m_layers.reserve(ids.m_layers.size() + 1);
    ids.m_rootLayerOf.reserve(ids.m_rootLayerOf.size() + 1);
    m_handles.layer = {ids.m_authority->launch(), ids.m_layerDomain, serial};
  } else {
    m_handles.layer = ids.handleOf(layer);
    if (!m_handles.layer.valid())
      throw std::runtime_error("HDRI destination Layer is not registered");
  }
  decltype(ids.m_nodeSerialOf) serials;
  decltype(ids.m_nodes) nodes;
  const std::uint64_t lifetimes[] = {
      m_transaction.m_rootLifetime, m_transaction.m_lightNodeLifetime};
  identity::NodeHandle *handles[] = {
      &m_handles.contentRoot, &m_handles.lightNode};
  for (size_t i = 0; i < 2; ++i) {
    const auto serial = ids.m_authority->nextSerial();
    const auto key = std::make_pair(m_handles.layer.serial, lifetimes[i]);
    serials.emplace(key, serial);
    nodes.emplace(serial,
        identity::SceneIdentity::NodeEntry{m_handles.layer.serial,
            lifetimes[i],
            m_transaction.m_nodeExtent + i});
    m_nodeSerials[i] = serials.extract(key);
    m_nodes[i] = nodes.extract(serial);
    *handles[i] = {ids.m_authority->launch(), ids.m_nodeDomain, serial};
  }
  ids.m_nodes.reserve(ids.m_nodes.size() + 2);
}

inline bool PreparedHdriPublication::isCurrent() const
{
  if (m_registration.expired() || !m_transaction.isCurrent())
    return false;
  const auto &ids = *m_identity;
  if (!fits(ids.m_serialOf, 2) || !fits(ids.m_entries, 2)
      || !fits(ids.m_nodes, 2))
    return false;
  for (size_t i = 0; i < 2; ++i)
    if (ids.m_serialOf.count(m_serials[i].key())
        || ids.m_entries.count(m_objects[i].key())
        || ids.m_nodeSerialOf.count(m_nodeSerials[i].key())
        || ids.m_nodes.count(m_nodes[i].key()))
      return false;
  if (!m_layer.empty()) {
    if (!fits(ids.m_layerSerialOf, 1) || !fits(ids.m_layers, 1)
        || !fits(ids.m_rootLayerOf, 1)
        || ids.m_layerSerialOf.count(m_layerSerial.key())
        || ids.m_layers.count(m_layer.key())
        || ids.m_rootLayerOf.count(m_layerRoot.key()))
      return false;
  } else if (!ids.resolve(m_handles.layer))
    return false;
  return true;
}

inline bool PreparedHdriPublication::committed() const
{
  return m_transaction.committed();
}

inline const HdriHandles &PreparedHdriPublication::handles() const
{
  return m_handles;
}

inline const std::string &PreparedHdriPublication::inventoryReceipt() const
{
  return m_receipt;
}

inline bool PreparedHdriPublication::commit()
{
  if (!isCurrent())
    return false;
  // Both guards are allocation-free and callback-free. Nothing can consume
  // reserved buckets or change predicted Forest stamps between them.
  if (!m_transaction.commit())
    return false;
  auto &ids = *m_identity;
  for (size_t i = 0; i < 2; ++i) {
    ids.m_serialOf.insert(std::move(m_serials[i]));
    ids.m_entries.insert(std::move(m_objects[i]));
    ids.m_nodeSerialOf.insert(std::move(m_nodeSerials[i]));
    ids.m_nodes.insert(std::move(m_nodes[i]));
    ids.advanceRevision(ids.m_sceneRevision);
    ids.inspectionChanged();
  }
  if (!m_layer.empty()) {
    ids.m_layerSerialOf.insert(std::move(m_layerSerial));
    ids.m_layers.insert(std::move(m_layer));
    ids.m_rootLayerOf.insert(std::move(m_layerRoot));
  }
  return true;
}

inline void PreparedHdriPublication::notify()
{
  m_transaction.notify();
}

} // namespace vsr::integration::content
