// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "vsr/io/PrincipledVolumeImport.hpp"
#include <array>

namespace vsr::ui::imgui {
class Application;

// Editable import description shared by manual mapping and JSON entry paths.
// Reading a description never imports scene objects; errors preserve edits.
struct PrincipledVolumeImportEditor
{
  const vsr::io::PrincipledVolumeImportOptions &options() const;
  void setOptions(const vsr::io::PrincipledVolumeImportOptions &);
  void readDescription(const std::filesystem::path &);
  void exportDescription(const std::filesystem::path &) const;
  std::string validationError() const;
  void buildUI(Application *);

 private:
  vsr::io::PrincipledVolumeImportOptions m_options;
  vsr::math::box3 m_manualBounds{
      vsr::math::float3(0.f), vsr::math::float3(1.f)};
  std::array<std::string, 4> m_browseResults;
  std::string m_descriptionPath;
  std::string m_descriptionResult;
  std::string m_exportResult;
  std::string m_error;
  bool m_confirmOverwrite{false};
};
} // namespace vsr::ui::imgui
