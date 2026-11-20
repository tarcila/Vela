// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PrincipledVolumeImportEditor.h"
#include "vsr/ui/imgui/Application.h"
#include <misc/cpp/imgui_stdlib.h>

namespace vsr::ui::imgui {
namespace {
constexpr const char *FIELDS[] = {
    "density", "temperature", "scattering", "emission"};
}

const vsr::io::PrincipledVolumeImportOptions &
PrincipledVolumeImportEditor::options() const
{
  return m_options;
}

void PrincipledVolumeImportEditor::setOptions(
    const vsr::io::PrincipledVolumeImportOptions &options)
{
  vsr::io::validatePrincipledVolumeOptions(options);
  m_options = options;
  if (options.bounds)
    m_manualBounds = *options.bounds;
  // Outstanding browse completions must not override a new JSON mapping.
  // Buffers retain stable addresses for SDL's asynchronous callbacks.
  for (auto &result : m_browseResults)
    result.clear();
}

void PrincipledVolumeImportEditor::readDescription(
    const std::filesystem::path &path)
{
  setOptions(vsr::io::readPrincipledVolumeDescription(path));
}

void PrincipledVolumeImportEditor::exportDescription(
    const std::filesystem::path &path) const
{
  vsr::io::writePrincipledVolumeDescription(m_options, path);
}

std::string PrincipledVolumeImportEditor::validationError() const
{
  try {
    vsr::io::validatePrincipledVolumeOptions(m_options);
    if (!m_options.bounds && m_options.fields.empty())
      return "Set bounds for a uniform medium, or select at least one field.";
    for (const auto &[name, source] : m_options.fields)
      if (!std::filesystem::is_regular_file(source.file))
        return name + ": field file does not exist: " + source.file.string();
  } catch (const std::exception &e) {
    return e.what();
  }
  return {};
}

void PrincipledVolumeImportEditor::buildUI(Application *app)
{
  if (!m_descriptionResult.empty()) {
    m_descriptionPath = std::move(m_descriptionResult);
    m_descriptionResult.clear();
    try {
      readDescription(m_descriptionPath);
      m_error.clear();
    } catch (const std::exception &e) {
      m_error = e.what();
    }
  }
  if (!m_exportResult.empty() && !m_confirmOverwrite) {
    try {
      if (std::filesystem::exists(m_exportResult))
        m_confirmOverwrite = true;
      else {
        exportDescription(m_exportResult);
        m_exportResult.clear();
        m_error.clear();
      }
    } catch (const std::exception &e) {
      m_error = e.what();
      m_exportResult.clear();
    }
  }
  ImGui::InputText("description JSON", &m_descriptionPath);
  if (ImGui::Button("Read JSON")) {
    try {
      readDescription(m_descriptionPath);
      m_error.clear();
    } catch (const std::exception &e) {
      m_error = e.what();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse JSON..."))
    app->getFilenameFromDialog(m_descriptionResult);
  ImGui::SameLine();
  ImGui::BeginDisabled(!validationError().empty() || m_confirmOverwrite);
  if (ImGui::Button("Export JSON..."))
    app->getFilenameFromDialog(m_exportResult, true);
  ImGui::EndDisabled();
  if (m_confirmOverwrite) {
    ImGui::TextWrapped(
        "Replace existing description '%s'?", m_exportResult.c_str());
    if (ImGui::Button("Replace")) {
      try {
        exportDescription(m_exportResult);
        m_error.clear();
      } catch (const std::exception &e) {
        m_error = e.what();
      }
      m_exportResult.clear();
      m_confirmOverwrite = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Keep existing")) {
      m_exportResult.clear();
      m_confirmOverwrite = false;
    }
  }
  ImGui::Separator();
  ImGui::InputText("medium name", &m_options.name);
  ImGui::TextUnformatted("Field files (RGB fields must contain linear RGB)");
  ImGui::TextWrapped(
      "Density and temperature use scalar fields. Scattering "
      "and emission accept scalar or RGB fields. Values multiply "
      "field samples; scalar samples broadcast across RGB.");
  for (size_t i = 0; i < 4; ++i) {
    auto name = FIELDS[i];
    ImGui::PushID(name);
    if (!m_browseResults[i].empty()) {
      m_options.fields[name] = {m_browseResults[i]};
      m_browseResults[i].clear();
    }
    auto found = m_options.fields.find(name);
    std::string path =
        found == m_options.fields.end() ? "" : found->second.file.string();
    if (ImGui::InputText(name, &path)) {
      if (path.empty())
        m_options.fields.erase(name);
      else
        m_options.fields[name] = {path};
    }
    ImGui::SameLine();
    if (ImGui::Button("..."))
      app->getFilenameFromDialog(m_browseResults[i]);
    ImGui::SameLine();
    if (ImGui::Button("clear"))
      m_options.fields.erase(name);
    ImGui::PopID();
  }
  bool manual = m_options.bounds.has_value();
  if (ImGui::Checkbox("manual bounds", &manual)) {
    if (manual)
      m_options.bounds = m_manualBounds;
    else
      m_options.bounds.reset();
  }
  if (manual) {
    ImGui::InputFloat3("lower", &m_manualBounds.lower.x);
    ImGui::InputFloat3("upper", &m_manualBounds.upper.x);
    m_options.bounds = m_manualBounds;
  } else
    ImGui::TextDisabled("Bounds: union of field sampling domains");

  // Display the same defaults the imported Volume uses. Keep unedited values
  // absent from options so the extension defaults remain authoritative.
  vsr::scene::Volume defaults(vsr::scene::tokens::volume::principled);
  if (ImGui::CollapsingHeader(
          "Values / field multipliers", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (size_t i = 0; i < defaults.numParameters(); ++i) {
      const auto &p = defaults.parameterAt(i);
      auto name = std::string(defaults.parameterNameAt(i));
      vsr::io::PrincipledVolumeValue value;
      switch (p.value().type()) {
      case ANARI_FLOAT32:
        value = p.value().get<float>();
        break;
      case ANARI_FLOAT32_VEC3:
        value = p.value().get<vsr::math::float3>();
        break;
      case ANARI_BOOL:
        value = p.value().get<bool>();
        break;
      default:
        continue;
      }
      if (auto found = m_options.parameters.find(name);
          found != m_options.parameters.end())
        value = found->second;
      bool changed = std::visit(
          [&](auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>)
              return ImGui::InputFloat(name.c_str(), &v);
            else if constexpr (std::is_same_v<T, vsr::math::float3>)
              return ImGui::InputFloat3(name.c_str(), &v.x);
            else if constexpr (std::is_same_v<T, bool>)
              return ImGui::Checkbox(name.c_str(), &v);
            else
              return ImGui::InputScalar(name.c_str(), ImGuiDataType_U32, &v);
          },
          value);
      if (changed)
        m_options.parameters[name] = value;
    }
    // ID has no extension default, so expose it as an optional override.
    bool hasId = m_options.parameters.count("id");
    if (ImGui::Checkbox("set auxiliary ID", &hasId)) {
      if (hasId)
        m_options.parameters["id"] = uint32_t(0);
      else
        m_options.parameters.erase("id");
    }
    if (hasId)
      ImGui::InputScalar("id",
          ImGuiDataType_U32,
          &std::get<uint32_t>(m_options.parameters.at("id")));
  }
  if (m_options.fields.count("emission")) {
    auto value = m_options.parameters.find("emission");
    auto rgb = value == m_options.parameters.end()
        ? vsr::math::float3(0.f)
        : std::get<vsr::math::float3>(value->second);
    if (rgb.x == 0 && rgb.y == 0 && rgb.z == 0)
      ImGui::TextWrapped(
          "Emission field is disabled: set a nonzero emission value.");
  }
  if (m_options.fields.count("temperature")) {
    auto value = m_options.parameters.find("blackbodyIntensity");
    if (value == m_options.parameters.end()
        || std::get<float>(value->second) == 0)
      ImGui::TextWrapped(
          "Thermal emission is disabled: blackbodyIntensity is zero.");
    ImGui::TextWrapped(
        "Thermal emission also requires absorption: white "
        "effective scattering albedo or zero density disables it.");
  }
  if (!m_error.empty())
    ImGui::TextWrapped("%s", m_error.c_str());
}
} // namespace vsr::ui::imgui
