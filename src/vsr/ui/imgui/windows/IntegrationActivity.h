// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Viewer window over the application-owned agent-integration state (spec
// §13). Only compiled with VSR_USE_INTEGRATION; the viewer owns the
// integration, this window only reads it and forwards local controls.

#include "IntegrationActivityModel.h"
#include "Window.h"
// std
#include <optional>
#include <string>

namespace vsr::ui::imgui {

struct IntegrationActivity : public Window
{
  IntegrationActivity(Application *app, IntegrationServices services);

  void buildUI() override;

  IntegrationActivityModel &model();

 private:
  struct PendingLocal
  {
    enum class Kind
    {
      RESTORE,
      RESYNCHRONIZE
    } kind{Kind::RESTORE};
    vsr::integration::operations::LocalRef ref;
    std::string label;
  };

  void buildConnection(const IntegrationActivityState &s);
  void buildScenes(const IntegrationActivityState &s);
  void buildActivity(const IntegrationActivityState &s);
  void pollLocal();

  IntegrationActivityModel m_model;
  std::optional<PendingLocal> m_pending;
  std::string m_lastLocal; // outcome text of the last local action
  bool m_confirmDisconnect{false};
};

} // namespace vsr::ui::imgui
