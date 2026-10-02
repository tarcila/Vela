// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <unordered_map>

namespace vsr::integration::identity {

/*
 * External identity and revision tokens for the agent integration.
 *
 * Raw storage indices are never external identity. An IdentityAuthority is
 * created once per activation (launch); every handle and token it issues
 * carries that activation's stamp, so nothing from an earlier activation can
 * resolve in a later one. A SceneIdentity observes one registered Scene and
 * issues:
 *
 *   - ObjectHandle: never reused and never redirected within the authority.
 *     Removal or scene replacement invalidates; defragmentation remaps.
 *   - ParameterVersionToken: changes on every parameter change of that object,
 *     including change-back-to-original. Metadata/labels are not parameters.
 *   - SceneRevision: render-relevant scene content (objects other than
 *     cameras/renderers, layers, transforms, arrays).
 *   - InspectionRevision: everything inspection returns, including metadata,
 *     names, membership/order, handle mapping and registration. Can change
 *     without the scene revision or any parameter version changing.
 *
 * ViewRevisionTracker covers a view's camera, renderer, dimensions and Image
 * Pipeline configuration.
 *
 * Threading: not thread-safe. Scene mutation and every query must happen on
 * the host's safe-point thread (see Integration::service()). A SceneIdentity
 * must be destroyed before the Scene it observes.
 */

struct LaunchStamp
{
  std::uint64_t activation{0}; // process-unique, monotonically assigned
  std::uint64_t nonce{0}; // random per activation

  friend bool operator==(const LaunchStamp &a, const LaunchStamp &b)
  {
    return a.activation == b.activation && a.nonce == b.nonce;
  }
  friend bool operator!=(const LaunchStamp &a, const LaunchStamp &b)
  {
    return !(a == b);
  }
};

struct ObjectHandle
{
  LaunchStamp launch;
  std::uint64_t domain{0}; // SceneIdentity instance within the launch
  std::uint64_t serial{0}; // 0 is never issued

  bool valid() const
  {
    return serial != 0;
  }
  friend bool operator==(const ObjectHandle &a, const ObjectHandle &b)
  {
    return a.launch == b.launch && a.domain == b.domain && a.serial == b.serial;
  }
  friend bool operator!=(const ObjectHandle &a, const ObjectHandle &b)
  {
    return !(a == b);
  }
};

enum class HandleKind : std::uint64_t
{
  OTHER = 0,
  OBJECT = 1,
  LAYER = 2,
  NODE = 3
};

// Distinct C++ types and wire domains; core lifetime stamps are never exposed.
template <typename Tag>
struct HierarchyHandle
{
  LaunchStamp launch;
  std::uint64_t domain{0};
  std::uint64_t serial{0};
  bool valid() const
  {
    return serial != 0;
  }
  friend bool operator==(const HierarchyHandle &a, const HierarchyHandle &b)
  {
    return a.launch == b.launch && a.domain == b.domain && a.serial == b.serial;
  }
  friend bool operator!=(const HierarchyHandle &a, const HierarchyHandle &b)
  {
    return !(a == b);
  }
};
struct LayerHandleTag;
struct NodeHandleTag;
using LayerHandle = HierarchyHandle<LayerHandleTag>;
using NodeHandle = HierarchyHandle<NodeHandleTag>;

// Fixed-size incremental chain observation. Each advance charges one ancestor
// at the caller's safe point; callers must abort/restart on inspection revision
// changes between slices. No recursive walk or retained scene snapshot.
struct NodeDependencyWalk
{
  NodeHandle node;
  LayerHandle layer;
  std::uint64_t ancestorSlot{0}; // slot+1, zero means complete
  std::uint64_t structuralVersion{0};
  std::uint64_t transformVersion{0};

  bool matchesStructure(const NodeDependencyWalk &other) const
  {
    return !ancestorSlot && !other.ancestorSlot && node == other.node
        && layer == other.layer && structuralVersion == other.structuralVersion;
  }
  bool matchesAncestry(const NodeDependencyWalk &other) const
  {
    return matchesStructure(other)
        && transformVersion == other.transformVersion;
  }
};

struct NodeEditVersionToken
{
  NodeHandle node;
  std::uint64_t version{0};

  friend bool operator==(
      const NodeEditVersionToken &a, const NodeEditVersionToken &b)
  {
    return a.node == b.node && a.version == b.version;
  }
  friend bool operator!=(
      const NodeEditVersionToken &a, const NodeEditVersionToken &b)
  {
    return !(a == b);
  }
};

struct ParameterVersionToken
{
  ObjectHandle object;
  std::uint64_t version{0};

  friend bool operator==(
      const ParameterVersionToken &a, const ParameterVersionToken &b)
  {
    return a.object == b.object && a.version == b.version;
  }
  friend bool operator!=(
      const ParameterVersionToken &a, const ParameterVersionToken &b)
  {
    return !(a == b);
  }
};

// Distinct revision kinds must not be interchangeable.
template <typename TAG>
struct Revision
{
  LaunchStamp launch;
  std::uint64_t domain{0};
  std::uint64_t value{0};

  friend bool operator==(const Revision &a, const Revision &b)
  {
    return a.launch == b.launch && a.domain == b.domain && a.value == b.value;
  }
  friend bool operator!=(const Revision &a, const Revision &b)
  {
    return !(a == b);
  }
};

struct SceneRevisionTag;
struct ViewRevisionTag;
struct InspectionRevisionTag;
using SceneRevision = Revision<SceneRevisionTag>;
using ViewRevision = Revision<ViewRevisionTag>;
using InspectionRevision = Revision<InspectionRevisionTag>;

class IdentityAuthority
{
 public:
  IdentityAuthority();

  const LaunchStamp &launch() const;
  // Unique within this authority; never 0, never reused.
  std::uint64_t nextDomain(HandleKind kind = HandleKind::OTHER);
  static HandleKind handleKind(std::uint64_t domain)
  {
    return HandleKind(domain & 3);
  }
  std::uint64_t nextSerial();

 private:
  LaunchStamp m_launch;
  std::uint64_t m_nextDomain{1};
  std::uint64_t m_nextSerial{1};
};

class SceneIdentity
{
 public:
  SceneIdentity(IdentityAuthority &authority, scene::Scene &scene);
  ~SceneIdentity();

  SceneIdentity(const SceneIdentity &) = delete;
  SceneIdentity &operator=(const SceneIdentity &) = delete;

  scene::Scene *scene() const;

  // Handles //

  ObjectHandle handleOf(const scene::Object *o) const;
  // nullptr for foreign, removed, replaced or earlier-launch handles.
  scene::Object *resolve(const ObjectHandle &h) const;

  LayerHandle handleOf(const scene::Layer *layer) const;
  scene::Layer *resolve(const LayerHandle &h) const;
  NodeHandle rootHandle(const scene::Layer *layer) const;
  NodeHandle handleOf(scene::LayerNodeRef node) const;
  scene::LayerNodeRef resolve(const NodeHandle &h) const;

  std::optional<NodeDependencyWalk> beginNodeDependencies(
      const NodeHandle &h) const;
  bool advanceNodeDependencies(NodeDependencyWalk &walk) const;

  // Local node editable versions; no query-time fingerprints or retained
  // observations. Core history survives coalesced/absent notifications.
  std::optional<NodeEditVersionToken> nodeEditVersion(
      const NodeHandle &h) const;
  bool isCurrent(const NodeEditVersionToken &t) const;

  // Parameter versions //

  std::optional<ParameterVersionToken> parameterVersion(
      const ObjectHandle &h) const;
  // True only if the token names a live object and no parameter change on
  // that object happened since the token was issued.
  bool isCurrent(const ParameterVersionToken &t) const;

  // Revisions //

  SceneRevision sceneRevision() const;
  InspectionRevision inspectionRevision() const;

  // Name and metadata changes arrive incrementally through
  // BaseUpdateDelegate::signalObjectMetadataChanged. These explicit refreshes
  // remain for hosts that change names or metadata without emitting that
  // signal; they return true on a change. refreshAllMetadata() is O(objects).
  bool refreshMetadata(const ObjectHandle &h);
  bool refreshAllMetadata();

  // Host registration/membership changes relevant to inspection.
  void noteRegistrationChanged();

 private:
  struct Key
  {
    anari::DataType type{ANARI_UNKNOWN};
    std::size_t index{0};
    friend bool operator==(const Key &a, const Key &b)
    {
      return a.type == b.type && a.index == b.index;
    }
  };
  struct KeyHash
  {
    std::size_t operator()(const Key &k) const
    {
      return std::hash<std::size_t>{}(k.index) * 31u
          ^ std::hash<int>{}(int(k.type));
    }
  };
  struct Entry
  {
    Key key;
    std::uint64_t parameterVersion{1};
    std::size_t metadataFingerprint{0};
  };

  struct LayerEntry
  {
    scene::Layer *layer{nullptr};
    std::uint64_t lifetime{0};
    std::uint64_t rootLifetime{0};
    std::uint64_t rootSerial{0};
  };
  void trackLayer(const scene::Layer *layer);
  void untrackLayer(const scene::Layer *layer);

  class Observer;
  friend class Observer;

  static anari::DataType normalize(anari::DataType t);
  static bool isViewObject(anari::DataType t);
  static std::size_t fingerprint(const scene::Object &o);

  Entry *entryOf(const scene::Object *o);
  void track(const scene::Object *o);
  void untrack(const scene::Object *o);
  void untrackAll();
  void parameterChanged(const scene::Object *o);
  void metadataChanged(const scene::Object *o);
  void sceneContentChanged();
  void inspectionChanged();
  void remap(const scene::IndexRemapper &remapper);
  void trackExisting();

  IdentityAuthority *m_authority{nullptr};
  scene::Scene *m_scene{nullptr};
  std::uint64_t m_domain{0};
  std::uint64_t m_layerDomain{0};
  std::uint64_t m_nodeDomain{0};
  std::unordered_map<std::uint64_t, std::uint64_t> m_layerSerialOf;
  std::unordered_map<std::uint64_t, LayerEntry> m_layers;
  std::unordered_map<std::uint64_t, std::uint64_t> m_rootLayerOf;
  struct NodeEntry
  {
    std::uint64_t layerSerial;
    std::uint64_t lifetime;
    std::size_t index;
  };
  // Forest creation stamps, not addresses or payloads, establish identity.
  mutable std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t>
      m_nodeSerialOf;
  mutable std::unordered_map<std::uint64_t, NodeEntry> m_nodes;
  Observer *m_observer{nullptr};
  std::size_t m_defragToken{0};
  std::uint64_t m_sceneRevision{1};
  std::uint64_t m_inspectionRevision{1};
  std::unordered_map<Key, std::uint64_t, KeyHash> m_serialOf;
  std::unordered_map<std::uint64_t, Entry> m_entries;
};

class ViewRevisionTracker
{
 public:
  ViewRevisionTracker(IdentityAuthority &authority, SceneIdentity &scene);

  void setCamera(const ObjectHandle &camera);
  void setRenderer(const ObjectHandle &renderer);
  void setDimensions(std::uint32_t width, std::uint32_t height);
  // Host reports any Image Pipeline configuration change for this view.
  void notePipelineChanged();

  // Folds in camera/renderer parameter changes observed since the last call.
  ViewRevision revision();

 private:
  struct Bound
  {
    ObjectHandle handle;
    std::optional<ParameterVersionToken> seen;
  };

  void bind(Bound &b, const ObjectHandle &h);
  bool poll(Bound &b) const;

  IdentityAuthority *m_authority{nullptr};
  SceneIdentity *m_scene{nullptr};
  std::uint64_t m_domain{0};
  std::uint64_t m_value{1};
  Bound m_camera;
  Bound m_renderer;
  std::uint32_t m_width{0};
  std::uint32_t m_height{0};
};

///////////////////////////////////////////////////////////////////////////////
// Inlined definitions ////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// IdentityAuthority //

inline IdentityAuthority::IdentityAuthority()
{
  static std::atomic<std::uint64_t> s_activations{0};
  m_launch.activation = ++s_activations;
  std::random_device rd;
  m_launch.nonce = (std::uint64_t(rd()) << 32) ^ std::uint64_t(rd());
}

inline const LaunchStamp &IdentityAuthority::launch() const
{
  return m_launch;
}

inline std::uint64_t IdentityAuthority::nextDomain(HandleKind kind)
{
  if (m_nextDomain > (UINT64_MAX >> 2))
    std::terminate();
  return (m_nextDomain++ << 2) | std::uint64_t(kind);
}

inline std::uint64_t IdentityAuthority::nextSerial()
{
  return m_nextSerial++;
}

// SceneIdentity::Observer //

class SceneIdentity::Observer : public scene::BaseUpdateDelegate
{
 public:
  explicit Observer(SceneIdentity *owner) : m_owner(owner) {}

  void signalObjectAdded(const scene::Object *o) override
  {
    m_owner->track(o);
  }
  void signalParameterUpdated(
      const scene::Object *o, const scene::Parameter *) override
  {
    m_owner->parameterChanged(o);
  }
  void signalParameterRemoved(
      const scene::Object *o, const scene::Parameter *) override
  {
    m_owner->parameterChanged(o);
  }
  void signalParameterBatchUpdated(const scene::Object *o,
      const std::vector<const scene::Parameter *> &) override
  {
    m_owner->parameterChanged(o);
  }
  void signalArrayMapped(const scene::Array *) override {}
  void signalArrayUnmapped(const scene::Array *a) override
  {
    m_owner->parameterChanged(a);
  }
  void signalObjectParameterUseCountZero(const scene::Object *) override {}
  void signalObjectLayerUseCountZero(const scene::Object *) override {}
  void signalObjectRemoved(const scene::Object *o) override
  {
    m_owner->untrack(o);
  }
  void signalRemoveAllObjects() override
  {
    m_owner->untrackAll();
  }
  void signalLayerAdded(const scene::Layer *layer) override
  {
    m_owner->trackLayer(layer);
    m_owner->sceneContentChanged();
  }
  void signalLayerStructureUpdated(const scene::Layer *) override
  {
    m_owner->sceneContentChanged();
  }
  void signalLayerTransformUpdated(const scene::Layer *) override
  {
    m_owner->sceneContentChanged();
  }
  void signalLayerRemoved(const scene::Layer *layer) override
  {
    m_owner->untrackLayer(layer);
    m_owner->sceneContentChanged();
  }
  void signalActiveLayersChanged() override
  {
    m_owner->sceneContentChanged();
  }
  void signalObjectFilteringChanged() override
  {
    m_owner->sceneContentChanged();
  }
  void signalObjectMetadataChanged(const scene::Object *o) override
  {
    m_owner->metadataChanged(o);
  }
  // Only emitted after defragmentation, which the defrag callback handles.
  void signalInvalidateCachedObjects() override {}

 private:
  SceneIdentity *m_owner{nullptr};
};

// SceneIdentity //

inline SceneIdentity::SceneIdentity(
    IdentityAuthority &authority, scene::Scene &scene)
    : m_authority(&authority),
      m_scene(&scene),
      m_domain(authority.nextDomain(HandleKind::OBJECT)),
      m_layerDomain(authority.nextDomain(HandleKind::LAYER)),
      m_nodeDomain(authority.nextDomain(HandleKind::NODE))
{
  trackExisting();
  for (const auto &entry : scene.layers())
    trackLayer(entry.second.ptr.get());
  m_observer = scene.updateDelegate().emplace<Observer>(this);
  m_defragToken = scene.addDefragCallback(
      [this](const scene::IndexRemapper &r) { remap(r); });
}

inline SceneIdentity::~SceneIdentity()
{
  m_scene->removeDefragCallback(m_defragToken);
  m_scene->updateDelegate().erase(m_observer);
}

inline scene::Scene *SceneIdentity::scene() const
{
  return m_scene;
}

inline void SceneIdentity::trackLayer(const scene::Layer *layer)
{
  if (!layer || m_layerSerialOf.count(layer->lifetime()))
    return;
  const auto serial = m_authority->nextSerial();
  const auto rootSerial = m_authority->nextSerial();
  m_layerSerialOf.emplace(layer->lifetime(), serial);
  m_layers.emplace(serial,
      LayerEntry{const_cast<scene::Layer *>(layer),
          layer->lifetime(),
          layer->root()->lifetime(),
          rootSerial});
  m_rootLayerOf.emplace(rootSerial, serial);
}

inline void SceneIdentity::untrackLayer(const scene::Layer *layer)
{
  auto it = m_layerSerialOf.find(layer->lifetime());
  if (it == m_layerSerialOf.end())
    return;
  m_rootLayerOf.erase(m_layers.at(it->second).rootSerial);
  m_layers.erase(it->second);
  m_layerSerialOf.erase(it);
}

inline LayerHandle SceneIdentity::handleOf(const scene::Layer *layer) const
{
  if (!layer || layer->scene() != m_scene)
    return {};
  auto it = m_layerSerialOf.find(layer->lifetime());
  if (it == m_layerSerialOf.end())
    return {};
  return {m_authority->launch(), m_layerDomain, it->second};
}

inline scene::Layer *SceneIdentity::resolve(const LayerHandle &h) const
{
  if (!h.valid() || h.launch != m_authority->launch()
      || h.domain != m_layerDomain)
    return nullptr;
  auto it = m_layers.find(h.serial);
  if (it == m_layers.end()
      || it->second.layer->lifetime() != it->second.lifetime)
    return nullptr;
  return it->second.layer;
}

inline NodeHandle SceneIdentity::rootHandle(const scene::Layer *layer) const
{
  const auto h = handleOf(layer);
  if (!h.valid())
    return {};
  const auto &entry = m_layers.at(h.serial);
  if (layer->root()->lifetime() != entry.rootLifetime)
    return {};
  return {m_authority->launch(), m_nodeDomain, entry.rootSerial};
}

inline NodeHandle SceneIdentity::handleOf(scene::LayerNodeRef node) const
{
  if (!node)
    return {};
  const auto *layer = node->value().layer();
  const auto layerHandle = handleOf(layer);
  if (!layerHandle.valid() || layer->at(node.index()) != node)
    return {};
  if (node == layer->root())
    return rootHandle(layer);
  const auto key = std::make_pair(layerHandle.serial, node->lifetime());
  auto it = m_nodeSerialOf.find(key);
  if (it == m_nodeSerialOf.end()) {
    const auto serial = m_authority->nextSerial();
    it = m_nodeSerialOf.emplace(key, serial).first;
    m_nodes.emplace(
        serial, NodeEntry{layerHandle.serial, node->lifetime(), node.index()});
  }
  return {m_authority->launch(), m_nodeDomain, it->second};
}

inline scene::LayerNodeRef SceneIdentity::resolve(const NodeHandle &h) const
{
  if (!h.valid() || h.launch != m_authority->launch()
      || h.domain != m_nodeDomain)
    return {};
  auto it = m_rootLayerOf.find(h.serial);
  if (it == m_rootLayerOf.end()) {
    const auto n = m_nodes.find(h.serial);
    if (n == m_nodes.end())
      return {};
    const auto layer = m_layers.find(n->second.layerSerial);
    if (layer == m_layers.end())
      return {};
    auto node = layer->second.layer->at(n->second.index);
    return node && node->lifetime() == n->second.lifetime
        ? node
        : scene::LayerNodeRef{};
  }
  const auto &entry = m_layers.at(it->second);
  auto root = entry.layer->root();
  return root->lifetime() == entry.rootLifetime ? root : scene::LayerNodeRef{};
}

inline std::optional<NodeDependencyWalk> SceneIdentity::beginNodeDependencies(
    const NodeHandle &h) const
{
  auto node = resolve(h);
  if (!node || handleOf(node) != h)
    return {};
  return NodeDependencyWalk{h,
      handleOf(node->value().layer()),
      node->isRoot() ? 0 : node->parent().index() + 1,
      node->topologyVersion(),
      0};
}

inline bool SceneIdentity::advanceNodeDependencies(
    NodeDependencyWalk &walk) const
{
  auto target = resolve(walk.node);
  auto *layer = resolve(walk.layer);
  if (!target || !layer || target->value().layer() != layer
      || layer->at(target.index()) != target)
    return false;
  if (!walk.ancestorSlot)
    return true;
  auto ancestor = layer->at(walk.ancestorSlot - 1);
  if (!ancestor)
    return false;
  walk.structuralVersion =
      std::max(walk.structuralVersion, ancestor->topologyVersion());
  walk.transformVersion =
      std::max(walk.transformVersion, ancestor->value().transformVersion());
  walk.ancestorSlot = ancestor->isRoot() ? 0 : ancestor->parent().index() + 1;
  return true;
}

inline std::optional<NodeEditVersionToken> SceneIdentity::nodeEditVersion(
    const NodeHandle &h) const
{
  auto node = resolve(h);
  if (!node)
    return {};
  return NodeEditVersionToken{h, node->value().editVersion()};
}

inline bool SceneIdentity::isCurrent(const NodeEditVersionToken &t) const
{
  auto current = nodeEditVersion(t.node);
  return current && *current == t;
}

inline ObjectHandle SceneIdentity::handleOf(const scene::Object *o) const
{
  if (!o || o->scene() != m_scene)
    return {};
  auto it = m_serialOf.find(Key{normalize(o->type()), o->index()});
  if (it == m_serialOf.end())
    return {};
  return ObjectHandle{m_authority->launch(), m_domain, it->second};
}

inline scene::Object *SceneIdentity::resolve(const ObjectHandle &h) const
{
  if (!h.valid() || h.launch != m_authority->launch() || h.domain != m_domain)
    return nullptr;
  auto it = m_entries.find(h.serial);
  if (it == m_entries.end())
    return nullptr;
  auto *o = m_scene->getObject(it->second.key.type, it->second.key.index);
  // Defensive: never hand back an object that does not own this slot.
  if (!o || normalize(o->type()) != it->second.key.type)
    return nullptr;
  return o;
}

inline std::optional<ParameterVersionToken> SceneIdentity::parameterVersion(
    const ObjectHandle &h) const
{
  if (!resolve(h))
    return std::nullopt;
  return ParameterVersionToken{h, m_entries.at(h.serial).parameterVersion};
}

inline bool SceneIdentity::isCurrent(const ParameterVersionToken &t) const
{
  auto current = parameterVersion(t.object);
  return current && *current == t;
}

inline SceneRevision SceneIdentity::sceneRevision() const
{
  return SceneRevision{m_authority->launch(), m_domain, m_sceneRevision};
}

inline InspectionRevision SceneIdentity::inspectionRevision() const
{
  return InspectionRevision{
      m_authority->launch(), m_domain, m_inspectionRevision};
}

inline bool SceneIdentity::refreshMetadata(const ObjectHandle &h)
{
  auto *o = resolve(h);
  if (!o)
    return false;
  auto &e = m_entries.at(h.serial);
  const auto fp = fingerprint(*o);
  if (fp == e.metadataFingerprint)
    return false;
  e.metadataFingerprint = fp;
  inspectionChanged();
  return true;
}

inline bool SceneIdentity::refreshAllMetadata()
{
  bool changed = false;
  for (auto &[serial, e] : m_entries) {
    auto *o = m_scene->getObject(e.key.type, e.key.index);
    if (!o)
      continue;
    const auto fp = fingerprint(*o);
    if (fp != e.metadataFingerprint) {
      e.metadataFingerprint = fp;
      changed = true;
    }
  }
  if (changed)
    inspectionChanged();
  return changed;
}

inline void SceneIdentity::noteRegistrationChanged()
{
  inspectionChanged();
}

inline anari::DataType SceneIdentity::normalize(anari::DataType t)
{
  return anari::isArray(t) ? ANARI_ARRAY : t;
}

inline bool SceneIdentity::isViewObject(anari::DataType t)
{
  return t == ANARI_CAMERA || t == ANARI_RENDERER;
}

inline std::size_t SceneIdentity::fingerprint(const scene::Object &o)
{
  // The metadata version is not preserved across object moves, so it is
  // combined with content that is; a spurious difference only over-reports.
  std::size_t h = std::hash<std::string>{}(o.name());
  h = h * 1099511628211ull ^ std::hash<std::uint64_t>{}(o.lastMetadataChange());
  h = h * 1099511628211ull ^ std::hash<std::size_t>{}(o.numMetadata());
  return h;
}

inline SceneIdentity::Entry *SceneIdentity::entryOf(const scene::Object *o)
{
  if (!o)
    return nullptr;
  auto it = m_serialOf.find(Key{normalize(o->type()), o->index()});
  return it == m_serialOf.end() ? nullptr : &m_entries.at(it->second);
}

inline void SceneIdentity::track(const scene::Object *o)
{
  if (!o)
    return;
  const Key key{normalize(o->type()), o->index()};
  // A stale mapping at a reused slot must never carry over to the newcomer.
  if (auto it = m_serialOf.find(key); it != m_serialOf.end())
    m_entries.erase(it->second);
  const auto serial = m_authority->nextSerial();
  m_serialOf[key] = serial;
  m_entries[serial] = Entry{key, 1, fingerprint(*o)};
  if (!isViewObject(key.type))
    m_sceneRevision++;
  inspectionChanged();
}

inline void SceneIdentity::untrack(const scene::Object *o)
{
  if (!o)
    return;
  const Key key{normalize(o->type()), o->index()};
  auto it = m_serialOf.find(key);
  if (it == m_serialOf.end())
    return;
  m_entries.erase(it->second);
  m_serialOf.erase(it);
  if (!isViewObject(key.type))
    m_sceneRevision++;
  inspectionChanged();
}

inline void SceneIdentity::untrackAll()
{
  m_serialOf.clear();
  m_entries.clear();
  m_sceneRevision++;
  inspectionChanged();
}

inline void SceneIdentity::parameterChanged(const scene::Object *o)
{
  auto *e = entryOf(o);
  if (!e)
    return;
  e->parameterVersion++;
  if (!isViewObject(e->key.type))
    m_sceneRevision++;
  inspectionChanged();
}

inline void SceneIdentity::metadataChanged(const scene::Object *o)
{
  auto *e = entryOf(o);
  if (!e)
    return;
  e->metadataFingerprint = fingerprint(*o);
  inspectionChanged();
}

inline void SceneIdentity::sceneContentChanged()
{
  m_sceneRevision++;
  inspectionChanged();
}

inline void SceneIdentity::inspectionChanged()
{
  m_inspectionRevision++;
}

inline void SceneIdentity::remap(const scene::IndexRemapper &remapper)
{
  // Handles survive defragmentation: re-key every serial to its new slot.
  std::unordered_map<Key, std::uint64_t, KeyHash> remapped;
  remapped.reserve(m_serialOf.size());
  for (auto &[serial, e] : m_entries) {
    e.key.index = remapper(e.key.type, e.key.index);
    remapped[e.key] = serial;
  }
  m_serialOf = std::move(remapped);
  // Enumeration order by storage slot may have changed.
  inspectionChanged();
}

inline void SceneIdentity::trackExisting()
{
  auto trackPool = [&](auto &pool) {
    for (std::size_t i = 0; i < pool.capacity(); i++) {
      if (auto ref = pool.at(i); ref)
        track(ref.data());
    }
  };
  auto &db = m_scene->objectDB();
  trackPool(db.array);
  trackPool(db.surface);
  trackPool(db.geometry);
  trackPool(db.material);
  trackPool(db.sampler);
  trackPool(db.volume);
  trackPool(db.field);
  trackPool(db.light);
  trackPool(db.camera);
  trackPool(db.renderer);
}

// ViewRevisionTracker //

inline ViewRevisionTracker::ViewRevisionTracker(
    IdentityAuthority &authority, SceneIdentity &scene)
    : m_authority(&authority), m_scene(&scene), m_domain(authority.nextDomain())
{}

inline void ViewRevisionTracker::setCamera(const ObjectHandle &camera)
{
  bind(m_camera, camera);
}

inline void ViewRevisionTracker::setRenderer(const ObjectHandle &renderer)
{
  bind(m_renderer, renderer);
}

inline void ViewRevisionTracker::setDimensions(
    std::uint32_t width, std::uint32_t height)
{
  if (width == m_width && height == m_height)
    return;
  m_width = width;
  m_height = height;
  m_value++;
}

inline void ViewRevisionTracker::notePipelineChanged()
{
  m_value++;
}

inline ViewRevision ViewRevisionTracker::revision()
{
  const bool cameraChanged = poll(m_camera);
  const bool rendererChanged = poll(m_renderer);
  if (cameraChanged || rendererChanged)
    m_value++;
  return ViewRevision{m_authority->launch(), m_domain, m_value};
}

inline void ViewRevisionTracker::bind(Bound &b, const ObjectHandle &h)
{
  if (b.handle == h)
    return;
  b.handle = h;
  b.seen = m_scene->parameterVersion(h);
  m_value++;
}

inline bool ViewRevisionTracker::poll(Bound &b) const
{
  if (!b.handle.valid())
    return false;
  auto current = m_scene->parameterVersion(b.handle);
  if (current == b.seen)
    return false;
  b.seen = current;
  return true;
}

} // namespace vsr::integration::identity
