// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/windows/TransferFunctionEditor.h"
// std
#include <memory>

namespace {

struct TestApplication : vsr::ui::imgui::Application
{
  const char *applicationIdentifier() const override;
  const char *getDefaultLayout() const override;
};

const char *TestApplication::applicationIdentifier() const
{
  return "tf-editor-test";
}

const char *TestApplication::getDefaultLayout() const
{
  return "";
}

} // namespace

TEST_CASE("TF editor ignores non-transfer-function volumes",
    "[TransferFunctionEditor]")
{
  using namespace vsr::scene;
  using namespace vsr::ui::imgui;

  auto context = std::unique_ptr<ImGuiContext, void (*)(ImGuiContext *)>(
      ImGui::CreateContext(), ImGui::DestroyContext);
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = ImVec2(800, 800);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

  TestApplication app;
  TransferFunctionEditor editor(&app);
  auto *ctx = app.appContext();
  auto &scene = ctx->vsr.scene;
  auto principled = scene.createObject<Volume>("principledVolume");
  principled->setParameter("density", 1.f);
  auto transfer =
      scene.createObject<Volume>(tokens::volume::transferFunction1D);
  auto root = scene.defaultLayer()->root();
  auto principledNode = scene.insertChildObjectNode(root, principled);
  auto transferNode = scene.insertChildObjectNode(root, transfer);

  auto draw = [&]() {
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(700, 700));
    ImGui::Begin("TF editor test");
    editor.buildUI();
    ImGui::End();
    ImGui::EndFrame();
  };

  SECTION("unsupported volume alone")
  {
    ctx->setSelected(principledNode);
    draw();
    REQUIRE(principled->parameter("color") == nullptr);
    REQUIRE(principled->parameter("opacity") == nullptr);
  }

  SECTION("mixed selection in either order")
  {
    ctx->setSelected(std::vector<LayerNodeRef>{principledNode, transferNode});
    draw();
    ctx->setSelected(std::vector<LayerNodeRef>{transferNode, principledNode});
    draw();
    REQUIRE(principled->parameter("color") == nullptr);
    REQUIRE(principled->parameter("opacity") == nullptr);
    REQUIRE(transfer->parameter("opacity") != nullptr);
  }

  SECTION("selection transitions")
  {
    ctx->setSelected(transferNode);
    draw();
    ctx->setSelected(principledNode);
    draw();
    ctx->clearSelected();
    draw();
    ctx->setSelected(transferNode);
    draw();
    REQUIRE(transfer->parameterValueAs<float>("opacity") == 1.f);
  }

  SECTION("transfer-function parameters may be removed")
  {
    transfer->removeParameter("opacity");
    transfer->removeParameter("unitDistance");
    transfer->removeParameter("valueRange");
    ctx->setSelected(transferNode);
    draw();
    REQUIRE(transfer->parameter("opacity") == nullptr);
    REQUIRE(transfer->parameter("unitDistance") == nullptr);
    REQUIRE(transfer->parameter("valueRange") == nullptr);
  }
}
