// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <atomic>
#include <exception>
#include <stdexcept>

namespace vsr::scene {

namespace {
std::uint64_t nextLayerLifetime()
{
  static std::atomic<std::uint64_t> next{1};
  const auto stamp = next.fetch_add(1, std::memory_order_relaxed);
  if (!stamp)
    std::terminate();
  return stamp;
}
} // namespace

Layer::Layer(Scene *scene, std::string name)
    : m_lifetime(nextLayerLifetime()),
      m_owner(std::make_shared<detail::LayerOwnerState>(
          detail::LayerOwnerState{this,
              scene ? scene->m_dependencyState
                    : std::weak_ptr<detail::SceneDependencyState>{}})),
      m_name(std::move(name)),
      m_tree(
          {this, math::IDENTITY_MAT4, "root"},
          [this](LayerNodeData &value) { value.bind(this); },
          [this]() { noteMembershipChange(); },
          [this](const LayerNodeData &value) { validateCopy(value); })
{}

Layer::~Layer()
{
  // Detached payloads can still resolve their Scene and balance references.
  m_owner->layer = nullptr;
}

Scene *Layer::scene() const
{
  auto owner = m_owner->scene.lock();
  return owner ? owner->scene : nullptr;
}

const std::string &Layer::name() const
{
  return m_name;
}

std::uint64_t Layer::metadataSequence() const
{
  return m_metadataSequence;
}

void Layer::setName(std::string name)
{
  noteMetadataChange();
  m_name = std::move(name);
}

void Layer::noteMetadataChange()
{
  if (++m_metadataSequence == 0)
    std::terminate();
  noteMembershipChange();
}

size_t Layer::size() const
{
  return m_tree.size();
}

size_t Layer::capacity() const
{
  return m_tree.capacity();
}

bool Layer::empty() const
{
  return m_tree.empty();
}

LayerNodeRef Layer::root() const
{
  return m_tree.root();
}

LayerNodeRef Layer::at(size_t i) const
{
  return m_tree.at(i);
}

void Layer::noteMembershipChange()
{
  if (!m_owner->dependencyAttached)
    return;
  if (auto owner = m_owner->scene.lock())
    owner->noteChange();
}

void Layer::validateCopy(const LayerNodeData &value) const
{
  // Compare ownership identities, not raw pointers or numeric object indices.
  // Expired owners must not be revived by allocator reuse in another Scene.
  if (m_owner->scene.owner_before(value.m_sceneOwner)
      || value.m_sceneOwner.owner_before(m_owner->scene))
    throw std::runtime_error("Cannot copy a subtree across Scenes");
}

void Layer::revokeMembership()
{
  noteMembershipChange();
  m_owner->dependencyAttached = false;
}

void Layer::erase(LayerNodeRef obj)
{
  m_tree.erase(obj);
}

void Layer::clear()
{
  m_tree.clear();
  if (auto *s = scene())
    s->signalLayerStructureChanged(this);
}

bool Layer::isAncestorOf(
    LayerNodeRef potentialAncestor, LayerNodeRef node) const
{
  return m_tree.isAncestorOf(potentialAncestor, node);
}

void Layer::traverse(LayerNodeRef start, Visitor &visitor)
{
  m_tree.traverse(start, visitor);
}

void Layer::traverse(LayerNodeRef start, VisitorEntryFunction &&f)
{
  m_tree.traverse(start, std::move(f));
}

void Layer::traverse(LayerNodeRef start,
    VisitorEntryFunction &&onNodeEntry,
    VisitorExitFunction &&onNodeExit)
{
  m_tree.traverse(start, std::move(onNodeEntry), std::move(onNodeExit));
}

void Layer::traverse_const(LayerNodeRef start, Visitor &visitor) const
{
  m_tree.traverse_const(start, visitor);
}

void Layer::traverse_const(
    LayerNodeRef start, ConstVisitorEntryFunction &&f) const
{
  m_tree.traverse_const(start, std::move(f));
}

void Layer::traverse_const(LayerNodeRef start,
    ConstVisitorEntryFunction &&onNodeEntry,
    ConstVisitorExitFunction &&onNodeExit) const
{
  m_tree.traverse_const(start, std::move(onNodeEntry), std::move(onNodeExit));
}

} // namespace vsr::scene
