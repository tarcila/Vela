// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/edit/AgentEdit.hpp"
#include "vsr/integration/mesh/MeshCapture.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedMeshBinding.hpp"

namespace vsr::integration::mesh {

class MeshCommitAccess
{
 public:
  static identity::ObjectHandle prepare(
      identity::SceneIdentity &ids, scene::Array &array);
  static void discard(identity::SceneIdentity &ids, scene::Array &array);
  static void committed(
      identity::SceneIdentity &ids, scene::Geometry &geometry);
};

inline identity::ObjectHandle MeshCommitAccess::prepare(
    identity::SceneIdentity &ids, scene::Array &array)
{
  const identity::SceneIdentity::Key key{
      ids.normalize(array.type()), array.index()};
  if (ids.m_serialOf.count(key))
    throw std::bad_alloc();
  const auto serial = ids.m_authority->nextSerial();
  auto entry = ids.m_entries.emplace(serial,
      identity::SceneIdentity::Entry{
          key, 1, ids.fingerprint(array), array.lifetime(), true});
  const auto cancel = [&ids, serial](identity::SceneIdentity::Entry *) {
    ids.m_entries.erase(serial);
  };
  std::unique_ptr<identity::SceneIdentity::Entry, decltype(cancel)> reservation(
      &entry.first->second, cancel);
  if (!ids.m_serialOf.emplace(key, serial).second)
    throw std::bad_alloc();
  // Both identity maps now own the entry; dismiss the erase-on-failure guard.
  (void)reservation.release();
  // Only the identity serial is burned. Preparation is not Scene content.
  return {ids.m_authority->launch(), ids.m_domain, serial};
}
inline void MeshCommitAccess::discard(
    identity::SceneIdentity &ids, scene::Array &array)
{
  const identity::SceneIdentity::Key key{
      ids.normalize(array.type()), array.index()};
  const auto serial = ids.m_serialOf.find(key);
  if (serial == ids.m_serialOf.end())
    return;
  const auto entry = ids.m_entries.find(serial->second);
  if (entry == ids.m_entries.end())
    return;
  const auto lifetime = entry->second.arrayLifetime.lock();
  if (lifetime && lifetime->array == &array) {
    ids.m_entries.erase(entry);
    ids.m_serialOf.erase(serial);
  }
}
inline void MeshCommitAccess::committed(
    identity::SceneIdentity &ids, scene::Geometry &geometry)
{
  ids.parameterChanged(&geometry);
}

inline std::string targetVersion(const scene::Geometry &geometry,
    const identity::ObjectHandle &handle,
    bool next = false)
{
  return "mesh:" + std::to_string(handle.launch.activation) + "."
      + std::to_string(handle.launch.nonce) + "."
      + std::to_string(handle.domain) + "." + std::to_string(handle.serial)
      + "." + std::to_string(geometry.lastParameterChange() + (next ? 1 : 0));
}

inline const char *current(const scene::Geometry &geometry,
    identity::SceneIdentity &ids,
    const CapturePlan &plan,
    const std::string &expected,
    const mcp::json::Value &dependencies)
{
  if (targetVersion(geometry, ids.handleOf(&geometry)) != expected)
    return "stale_target_version";
  if (dependencies.type != mcp::json::Value::Type::ARRAY
      || dependencies.a.size() != plan.arrayCount)
    return "stale_dependencies";
  for (std::size_t i = 0; i < plan.arrayCount; ++i) {
    const auto handle = ids.handleOf(plan.arrays[i]);
    char text[33];
    std::snprintf(text,
        sizeof(text),
        "%016llx%016llx",
        (unsigned long long)handle.domain,
        (unsigned long long)handle.serial);
    const auto version = ids.arrayDataVersion(handle);
    if (!version)
      return "host_write_contract_unavailable";
    std::size_t matches = 0;
    for (const auto &entry : dependencies.a) {
      const auto *wire = entry.get("handle"), *data = entry.get("data_version");
      if (wire && data && wire->isString() && data->isString()
          && wire->s == text && data->s == *version)
        ++matches;
    }
    if (matches != 1)
      return "stale_dependencies";
  }
  return nullptr;
}

/* Rebuild the plan at every checkpoint: reservation callbacks and pool growth
 * can change dependencies or relocate their pointers. Keep null-target, mapped
 * dependency and currency failure precedence identical across those guards. */
inline const char *currentPlan(const scene::Geometry *geometry,
    identity::SceneIdentity &ids,
    CapturePlan &capture,
    const std::string &expected,
    const mcp::json::Value &dependencies)
{
  capture = {};
  if (!geometry)
    return "stale_target_version";
  if (const auto *reason = plan(*geometry, capture, false))
    return reason;
  return current(*geometry, ids, capture, expected, dependencies);
}

/* Each notification is attempted after publication. A throwing consumer cannot
 * change the commit fact or prevent the update batch's closing attempt. */
inline bool notify(scene::Scene &scene,
    scene::Geometry &geometry,
    const scene::detail::PreparedMeshBinding &transaction,
    const std::vector<edit::MeshSnapshot::Binding> &bindings,
    const std::vector<scene::Array *> &added,
    const std::vector<scene::Array *> &old)
{
  bool failed = false;
  const auto deliver = [&](const auto &callback) {
    try {
      callback();
    } catch (...) {
      failed = true;
    }
  };
  deliver([&] { scene.beginUpdateBatch(); });
  for (auto *array : added)
    deliver([&] { scene.updateDelegate().signalObjectAdded(array); });
  // The established batch notification lets render indexes commit one complete
  // triangle Geometry. Removed-field signals follow that coherent refill for
  // consumers that distinguish absence from disabled presence.
  deliver([&] {
    scene.updateDelegate().signalParameterBatchUpdated(
        &geometry, transaction.parameters());
  });
  for (const auto &binding : bindings) {
    const auto *now = geometry.parameter(binding.name);
    const auto *before = transaction.prior(binding.name);
    if (!now && before)
      deliver([&] {
        scene.updateDelegate().signalParameterRemoved(&geometry, before);
      });
  }
  for (auto *array : old)
    if (array && array->useCount(scene::Object::UseKind::PARAMETER) == 0)
      deliver([&] { scene.signalObjectParameterUseCountZero(array); });
  deliver([&] { scene.endUpdateBatch(); });
  return failed;
}

inline const char *dependenciesCurrent(const edit::MeshSnapshot &snapshot)
{
  for (const auto &dependency : snapshot.dependencies) {
    const auto lifetime = dependency.lifetime.lock();
    if (!lifetime || !lifetime->array
        || lifetime->array->dataWriteSequence() != dependency.write)
      return "conflict";
    if (lifetime->array->isMapped())
      return "array_mapped";
  }
  return nullptr;
}

inline const char *restorable(
    const edit::MeshSnapshot &snapshot, scene::Geometry *geometry)
{
  if (snapshot.registration.expired() || !geometry
      || geometry->lastParameterChange() != snapshot.targetVersion)
    return "conflict";
  for (const auto &binding : snapshot.bindings) {
    const auto *field = geometry->parameter(binding.name);
    if (bool(field) != binding.present)
      return "conflict";
    if (!field)
      continue;
    const auto after = binding.after.lock();
    if (!after || !after->array || field->isEnabled() != binding.enabled
        || geometry->parameterValueAsObject<scene::Array>(binding.name)
            != after->array)
      return "conflict";
  }
  return dependenciesCurrent(snapshot);
}

} // namespace vsr::integration::mesh
