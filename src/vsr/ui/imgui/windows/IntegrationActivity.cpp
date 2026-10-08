// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IntegrationActivity.h"
// imgui
#include <imgui.h>
// std
#include <cstdio>

namespace vsr::ui::imgui {

namespace vi = vsr::integration;

namespace {

const char *eligibilityName(vi::activity::Eligibility e)
{
  switch (e) {
  case vi::activity::Eligibility::NONE:
    return "-";
  case vi::activity::Eligibility::ELIGIBLE:
    return "eligible";
  case vi::activity::Eligibility::RESTORED:
    return "restored";
  case vi::activity::Eligibility::EXPIRED:
    return "expired";
  case vi::activity::Eligibility::CONFLICTING:
    return "conflicting";
  }
  return "?";
}

ImVec4 syncColor(vi::sync::SyncState s)
{
  switch (s) {
  case vi::sync::SyncState::READY:
    return ImVec4(0.4f, 0.9f, 0.4f, 1.f);
  case vi::sync::SyncState::SYNCHRONIZING:
    return ImVec4(0.9f, 0.9f, 0.4f, 1.f);
  default:
    return ImVec4(1.f, 0.4f, 0.4f, 1.f);
  }
}

std::string stateText(const vi::activity::Entry &e)
{
  if (e.terminal) {
    std::string t = vi::operations::outcomeName(*e.terminal);
    // Committed-with-sync-failure is distinct from unchanged/rejected.
    t += e.committed ? " (committed)" : " (not committed)";
    return t;
  }
  if (e.progress)
    return vi::operations::progressName(*e.progress);
  return "requested";
}

std::string targetsText(const vi::activity::Entry &e)
{
  std::string out;
  for (auto &t : e.targets) {
    if (!out.empty())
      out += ", ";
    out += t.kind == vi::session::Target::Kind::SCENE ? "scene " : "view ";
    out += std::to_string(t.id);
  }
  return out.empty() ? "-" : out;
}

} // namespace

IntegrationActivity::IntegrationActivity(
    Application *app, IntegrationServices services)
    : Window(app, "Agent Activity"), m_model(std::move(services))
{}

IntegrationActivityModel &IntegrationActivity::model()
{
  return m_model;
}

void IntegrationActivity::buildUI()
{
  pollLocal();
  const auto &s = m_model.refresh();
  buildConnection(s);
  ImGui::Separator();
  buildScenes(s);
  ImGui::Separator();
  buildActivity(s);
}

void IntegrationActivity::buildConnection(const IntegrationActivityState &s)
{
  if (!s.active) {
    ImGui::TextDisabled("Agent integration inactive");
    return;
  }

  // Connection information: descriptor path only, never the credential.
  ImGui::Text("Descriptor:");
  ImGui::SameLine();
  if (s.descriptorPath.empty())
    ImGui::TextDisabled("not published");
  else {
    ImGui::TextUnformatted(s.descriptorPath.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy path"))
      ImGui::SetClipboardText(s.descriptorPath.c_str());
  }

  ImGui::Text("Ownership: %s",
      s.sessionOwned ? "client session attached" : "no client session");

  bool cameraEnabled = s.agentCameraControlEnabled;
  if (ImGui::Checkbox("Allow agent camera control", &cameraEnabled))
    m_model.setAgentCameraControlEnabled(cameraEnabled);

  bool disabled = s.agentEditsDisabled;
  if (ImGui::Checkbox("Disable agent edits", &disabled))
    m_model.setAgentEditsDisabled(disabled);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Rejects new agent edits/restorations and cancels queued ones; the "
        "session stays attached and may keep inspecting/rendering.");

  ImGui::SameLine();
  ImGui::BeginDisabled(!s.sessionOwned);
  if (!m_confirmDisconnect) {
    if (ImGui::Button("Disconnect"))
      m_confirmDisconnect = true;
  } else {
    if (ImGui::Button("Confirm disconnect")) {
      m_lastLocal = m_model.disconnect() ? "session revoked locally"
                                         : "no session to revoke";
      m_confirmDisconnect = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      m_confirmDisconnect = false;
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(
        "Local whole-session revocation: releases ownership and cancels "
        "outstanding work without client cooperation.");

  if (!m_lastLocal.empty())
    ImGui::TextDisabled("Last local action: %s", m_lastLocal.c_str());
}

void IntegrationActivity::buildScenes(const IntegrationActivityState &s)
{
  ImGui::TextUnformatted("Scenes");
  if (!ImGui::BeginTable("##integration_scenes",
          4,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    return;
  ImGui::TableSetupColumn("Scene");
  ImGui::TableSetupColumn("Synchronization");
  ImGui::TableSetupColumn("Client capabilities");
  ImGui::TableSetupColumn("Local");
  ImGui::TableHeadersRow();
  for (auto &row : s.scenes) {
    ImGui::PushID(int(row.id));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Text("%s (%u)", row.name.c_str(), unsigned(row.id));
    ImGui::TableNextColumn();
    ImGui::TextColored(
        syncColor(row.sync), "%s", vi::sync::syncStateName(row.sync));
    ImGui::TableNextColumn();
    ImGui::Text("%s%s%s%s",
        row.effective.read ? "read " : "",
        row.effective.render ? "render " : "",
        row.effective.edit ? "edit " : "",
        row.effective.resynchronize ? "resync" : "");
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(!row.localResynchronize || m_pending.has_value());
    if (ImGui::SmallButton("Resynchronize")) {
      auto a = m_model.resynchronize(row.id);
      if (a.code == vi::operations::Outcome::OK)
        m_pending = PendingLocal{PendingLocal::Kind::RESYNCHRONIZE,
            a.ref,
            "resynchronize " + row.name};
      else
        m_lastLocal = std::string("resynchronize rejected: ")
            + vi::operations::outcomeName(a.code);
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void IntegrationActivity::buildActivity(const IntegrationActivityState &s)
{
  ImGui::Text(
      "Activity (%zu records, %zu metadata bytes, %zu restorable, %zu image "
      "bytes)",
      s.usage.entries,
      s.usage.bytes,
      s.usage.restorableBatches,
      s.usage.imageBytes);
  ImGui::Text("Recovery: %llu eligible bytes; %llu pending bytes (%s)",
      (unsigned long long)s.usage.retentionBytes,
      (unsigned long long)s.usage.reservedRetentionBytes,
      s.usage.oversizedRetention ? "oversized entry alone" : "normal FIFO");

  if (s.recoveryProposal) {
    const auto &p = *s.recoveryProposal;
    ImGui::SeparatorText("Exceptional recovery approval (trusted host only)");
    ImGui::Text("%s; Scene %llu; original operation %s#%llu",
        p.identity.kind.c_str(),
        (unsigned long long)p.identity.target,
        p.identity.sessionId.substr(0, 8).c_str(),
        (unsigned long long)p.identity.originalRequest);
    ImGui::TextWrapped("Target/evidence: %s", p.targetDescription.c_str());
    ImGui::Text("Required: %llu bytes; soft: %llu; hard: %llu",
        (unsigned long long)p.requiredBytes,
        (unsigned long long)p.softBytes,
        (unsigned long long)p.hardBytes);
    ImGui::Text("Requested finite maximum: %llu; approved: %llu",
        (unsigned long long)p.requiredBytes,
        (unsigned long long)p.approvedMaximum);
    ImGui::Text(
        "Expires at monotonic %.1f (five minutes from refusal)", p.expiresAt);
    ImGui::Text("Canonical storage: %zu / %zu bytes",
        p.canonicalCharge,
        s.canonicalProposalByteCap);
    ImGui::TextWrapped(
        "One successful commit will evict ALL older recovery "
        "and retain this entry alone. Permanent limits do not change. "
        "Approval does not guarantee allocation or bypass physical, "
        "format, freshness or ordinary Scene READ/EDIT safeguards.");
    ImGui::BeginDisabled(p.claimed || p.approvedMaximum != 0);
    if (ImGui::Button("Approve this one operation")) {
      const bool approved =
          m_model.approveRecovery(p.identity, p.requiredBytes);
      m_lastLocal = approved
          ? "Recovery exception approved; retry the original "
            "proposal with a fresh request ID."
          : "Recovery approval rejected.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(p.approvedMaximum ? "Revoke exception" : "Decline")) {
      m_model.revokeRecovery(p.identity);
      m_lastLocal = "Recovery exception revoked/declined.";
    }
    ImGui::Separator();
  }

  const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
  if (!ImGui::BeginTable("##integration_activity", 8, flags))
    return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Operation");
  ImGui::TableSetupColumn("Owner");
  ImGui::TableSetupColumn("Targets");
  ImGui::TableSetupColumn("State / outcome");
  ImGui::TableSetupColumn("Batch Reference");
  ImGui::TableSetupColumn("Cleanup / held");
  ImGui::TableSetupColumn("Restoration");
  ImGui::TableSetupColumn("Client summary");
  ImGui::TableHeadersRow();

  // Newest first.
  for (auto it = s.activity.rbegin(); it != s.activity.rend(); ++it) {
    const auto &e = it->entry;
    ImGui::PushID(int(it->id));
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(vi::activity::operationKindName(e.kind));

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(it->reference.c_str());

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(targetsText(e).c_str());

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(stateText(e).c_str());
    if (!e.result.empty() && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", e.result.c_str());

    ImGui::TableNextColumn();
    if (e.batchRef)
      ImGui::TextUnformatted(e.batchRef->batchId.c_str());
    else
      ImGui::TextDisabled("-");

    ImGui::TableNextColumn();
    // Waiting time and held capacity only; never a claim the backend died.
    if (e.heldSlots > 0 || e.cleanupElapsedSeconds > 0.0)
      ImGui::Text("%.1fs / %zu slot(s)", e.cleanupElapsedSeconds, e.heldSlots);
    else
      ImGui::TextDisabled("-");

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(eligibilityName(e.eligibility));
    const bool restorable = e.batchRef
        && e.eligibility == vi::activity::Eligibility::ELIGIBLE
        && !e.targets.empty()
        && e.targets.front().kind == vi::session::Target::Kind::SCENE;
    if (restorable) {
      ImGui::SameLine();
      ImGui::BeginDisabled(m_pending.has_value());
      if (ImGui::SmallButton("Restore")) {
        auto a = m_model.restore(e.targets.front().id, *e.batchRef);
        if (a.code == vi::operations::Outcome::OK)
          m_pending = PendingLocal{PendingLocal::Kind::RESTORE,
              a.ref,
              "restore " + e.batchRef->batchId};
        else
          m_lastLocal = std::string("restore rejected: ")
              + vi::operations::outcomeName(a.code);
      }
      ImGui::EndDisabled();
    }

    ImGui::TableNextColumn();
    // Attributed client description, not a VSR-observed fact.
    if (e.clientSummary)
      ImGui::TextDisabled("client says: %s", e.clientSummary->text.c_str());
    else
      ImGui::TextDisabled("-");

    ImGui::PopID();
  }
  ImGui::EndTable();
}

void IntegrationActivity::pollLocal()
{
  if (!m_pending)
    return;
  const auto &svc = m_model.services();
  std::optional<vi::operations::Outcome> outcome;
  bool extraOk = true;
  if (m_pending->kind == PendingLocal::Kind::RESTORE && svc.restore) {
    if (auto r = svc.restore->resultLocal(m_pending->ref))
      outcome = r->outcome;
  } else if (svc.sync) {
    if (auto r = svc.sync->resultLocal(m_pending->ref)) {
      outcome = vi::operations::Outcome::OK;
      extraOk = r->ready;
    }
  }
  if (!outcome)
    return; // still queued; serviced at the viewer's safe point
  m_lastLocal = m_pending->label + ": " + vi::operations::outcomeName(*outcome)
      + (extraOk ? "" : " (views not confirmed)");
  m_pending.reset();
}

} // namespace vsr::ui::imgui
