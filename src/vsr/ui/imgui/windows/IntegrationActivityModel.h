// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ImGui-free view-model behind the viewer's integration activity window
// (spec §13). It only reads the application-owned integration state and
// forwards the local controls to the headless services, so it is testable
// without a display. Business rules stay in the integration services.

#include "vsr/integration/Integration.hpp"
#include "vsr/integration/activity/Activity.hpp"
#include "vsr/integration/restore/BatchRestore.hpp"
#include "vsr/integration/session/SessionAuthority.hpp"
#include "vsr/integration/sync/SceneSync.hpp"
// std
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vsr::ui::imgui {

// Non-owning pointers to the host's integration services. Any may be null;
// the corresponding view or control is then reported unavailable.
struct IntegrationServices
{
  vsr::integration::Integration *integration{nullptr};
  vsr::integration::session::SessionAuthority *authority{nullptr};
  vsr::integration::activity::ActivityLog *activity{nullptr};
  vsr::integration::sync::SyncService *sync{nullptr};
  vsr::integration::restore::RestoreService *restore{nullptr};
  // Scenes the host registered and wants listed, with display names.
  std::vector<std::pair<vsr::integration::SceneId, std::string>> scenes;
  // Published descriptor path; empty when nothing was published. Never the
  // launch credential.
  std::string descriptorPath;
};

struct IntegrationSceneRow
{
  vsr::integration::SceneId id{vsr::integration::INVALID_SCENE_ID};
  std::string name;
  vsr::integration::sync::SyncState sync{
      vsr::integration::sync::SyncState::READY};
  vsr::integration::session::EffectiveCapabilities effective;
  bool localEdit{false};
  bool localResynchronize{false};
};

struct IntegrationActivityRow
{
  vsr::integration::activity::ActivityLog::Id id{0};
  vsr::integration::activity::Entry entry;
  std::string reference; // "<session>#<request>" or "local#<n>"
};

struct IntegrationActivityState
{
  bool active{false};
  bool sessionOwned{false};
  bool agentEditsDisabled{false};
  std::string descriptorPath;
  std::vector<IntegrationSceneRow> scenes;
  std::vector<IntegrationActivityRow> activity; // oldest first
  vsr::integration::activity::Usage usage;
};

class IntegrationActivityModel
{
 public:
  explicit IntegrationActivityModel(IntegrationServices services)
      : m_services(std::move(services))
  {}

  const IntegrationServices &services() const
  {
    return m_services;
  }
  // Hosts that activate the integration after window creation (e.g. once
  // the scene finished loading) hand the services over here.
  void setServices(IntegrationServices services)
  {
    m_services = std::move(services);
  }

  // Re-reads every observable fact; call once per frame on the UI thread.
  const IntegrationActivityState &refresh();
  const IntegrationActivityState &state() const
  {
    return m_state;
  }

  // Local controls (host authority) //

  bool setAgentEditsDisabled(bool disabled);
  // Local whole-session revocation; false when no session was held.
  bool disconnect();
  vsr::integration::operations::LocalAdmission restore(
      vsr::integration::SceneId scene,
      const vsr::integration::edit::BatchRef &batch);
  vsr::integration::operations::LocalAdmission resynchronize(
      vsr::integration::SceneId scene);

 private:
  void refreshActivity();

  IntegrationServices m_services;
  IntegrationActivityState m_state;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const IntegrationActivityState &IntegrationActivityModel::refresh()
{
  namespace vi = vsr::integration;
  auto &s = m_state;
  auto *integration = m_services.integration;
  auto *authority = m_services.authority;

  s.active = integration && integration->active();
  s.descriptorPath = m_services.descriptorPath;
  if (authority)
    authority->expireIfDue();
  s.sessionOwned = authority && authority->hasSession();
  s.agentEditsDisabled = authority && authority->agentEditsDisabled();

  s.scenes.clear();
  for (auto &[id, name] : m_services.scenes) {
    IntegrationSceneRow row;
    row.id = id;
    row.name = name;
    if (m_services.sync)
      row.sync = m_services.sync->state(id);
    if (authority) {
      const auto t = vi::session::Target::scene(id);
      row.effective = authority->effectiveCapabilities(t);
      row.localEdit =
          authority->authorizeLocal(t, vi::session::Capability::EDIT);
      row.localResynchronize =
          authority->authorizeLocal(t, vi::session::Capability::RESYNCHRONIZE);
    }
    s.scenes.push_back(std::move(row));
  }

  refreshActivity();
  return s;
}

inline void IntegrationActivityModel::refreshActivity()
{
  auto &s = m_state;
  s.activity.clear();
  auto *log = m_services.activity;
  if (!log)
    return;

  for (auto &r : log->snapshot()) {
    IntegrationActivityRow row;
    row.id = r.id;
    const auto &e = r.entry;
    if (e.ref)
      row.reference =
          e.ref->sessionId.substr(0, 8) + "#" + std::to_string(e.ref->requestId);
    else if (e.localRef)
      row.reference = "local#" + std::to_string(e.localRef->id);
    row.entry = std::move(r.entry);
    s.activity.push_back(std::move(row));
  }
  s.usage = log->usage();
}

inline bool IntegrationActivityModel::setAgentEditsDisabled(bool disabled)
{
  if (!m_services.authority)
    return false;
  m_services.authority->setAgentEditsDisabled(disabled);
  return true;
}

inline bool IntegrationActivityModel::disconnect()
{
  return m_services.authority && m_services.authority->revokeSession();
}

inline vsr::integration::operations::LocalAdmission
IntegrationActivityModel::restore(vsr::integration::SceneId scene,
    const vsr::integration::edit::BatchRef &batch)
{
  if (!m_services.restore) {
    vsr::integration::operations::LocalAdmission a;
    a.code = vsr::integration::operations::Outcome::INVALID_REQUEST;
    return a;
  }
  return m_services.restore->submitLocal({scene, batch});
}

inline vsr::integration::operations::LocalAdmission
IntegrationActivityModel::resynchronize(vsr::integration::SceneId scene)
{
  if (!m_services.sync) {
    vsr::integration::operations::LocalAdmission a;
    a.code = vsr::integration::operations::Outcome::INVALID_REQUEST;
    return a;
  }
  return m_services.sync->submitLocalResynchronize(scene);
}

} // namespace vsr::ui::imgui
