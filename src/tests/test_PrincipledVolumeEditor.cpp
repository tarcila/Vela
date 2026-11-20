// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/modals/PrincipledVolumeImportEditor.h"
#include <fstream>

namespace {
struct EditorTestApp : vsr::ui::imgui::Application
{
  const char *applicationIdentifier() const override
  {
    return "principled-editor-test";
  }
  const char *getDefaultLayout() const override
  {
    return "";
  }
};
} // namespace

TEST_CASE(
    "Principled editor draws valid and invalid mappings without mutating the scene",
    "[PrincipledVolumeEditor]")
{
  auto *imgui = ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().DisplaySize = ImVec2(1200, 1200);
  ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  {
    EditorTestApp app;
    vsr::ui::imgui::PrincipledVolumeImportEditor editor;
    vsr::io::PrincipledVolumeImportOptions options;
    options.fields["emission"] = {"missing.nvdb"};
    editor.setOptions(options);
    for (int i = 0; i < 2; ++i) {
      ImGui::NewFrame();
      ImGui::Begin("Principled import test");
      editor.buildUI(&app);
      ImGui::End();
      ImGui::EndFrame();
      options.bounds =
          vsr::math::box3(vsr::math::float3(0.f), vsr::math::float3(1.f));
      editor.setOptions(options);
    }
    REQUIRE(app.appContext()->vsr.scene.numberOfObjects(ANARI_VOLUME) == 0);
    REQUIRE(editor.options().fields.at("emission").file == "missing.nvdb");
  }
  ImGui::DestroyContext(imgui);
}

TEST_CASE("Principled editor validates mappings without importing objects",
    "[PrincipledVolumeEditor]")
{
  vsr::ui::imgui::PrincipledVolumeImportEditor editor;
  REQUIRE_FALSE(editor.validationError().empty());
  vsr::io::PrincipledVolumeImportOptions o;
  o.bounds = vsr::math::box3(vsr::math::float3(0.f), vsr::math::float3(1.f));
  editor.setOptions(o);
  REQUIRE(editor.validationError().empty());
  o.fields["density"] = {"missing-field.nvdb"};
  editor.setOptions(o);
  REQUIRE(editor.validationError().find("density") != std::string::npos);
  REQUIRE(editor.options().fields.at("density").file == "missing-field.nvdb");
}

TEST_CASE("Principled editor preserves mapping after a bad JSON read",
    "[PrincipledVolumeEditor]")
{
  vsr::ui::imgui::PrincipledVolumeImportEditor editor;
  vsr::io::PrincipledVolumeImportOptions o;
  o.name = "keep";
  o.bounds = vsr::math::box3(vsr::math::float3(0.f), vsr::math::float3(1.f));
  editor.setOptions(o);
  auto file =
      std::filesystem::temp_directory_path() / "vsr-principled-editor.json";
  std::ofstream(file) << "{broken";
  REQUIRE_THROWS(editor.readDescription(file));
  REQUIRE(editor.options().name == "keep");
  std::ofstream(file)
      << R"({"name":"new","bounds":[0,0,0,1,2,3],"parameters":{"density":0.5}})";
  editor.readDescription(file);
  REQUIRE(editor.options().name == "new");
  REQUIRE(editor.validationError().empty());
  editor.exportDescription(file);
  auto copy = vsr::io::readPrincipledVolumeDescription(file);
  REQUIRE(std::get<float>(copy.parameters.at("density")) == 0.5f);
  std::filesystem::remove(file);
}
