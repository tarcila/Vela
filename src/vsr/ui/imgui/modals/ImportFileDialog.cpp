// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ImportFileDialog.h"
#include "vsr/ui/imgui/Application.h"
#include "vsr/ui/imgui/modals/BlockingTaskModal.h"
// SDL
#include <SDL3/SDL_dialog.h>
// vsr_io
#include "vsr/io/importers.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// imgui
#include <misc/cpp/imgui_stdlib.h>

namespace vsr::ui::imgui {

namespace {

// The label shown in the combo, paired with the type it selects. Pairing them
// is what keeps the two in step: the combo index used to be cast straight to
// ImporterType, so every entry after the first gap named one importer and ran
// another.
struct ImporterChoice
{
  const char *name;
  vsr::io::ImporterType type;
};

constexpr ImporterChoice IMPORTERS[] = {
    {"AGX", vsr::io::ImporterType::AGX},
    {"ANARI_CAPTURE", vsr::io::ImporterType::ANARI_CAPTURE},
    {"ASSIMP", vsr::io::ImporterType::ASSIMP},
    {"ASSIMP_FLAT", vsr::io::ImporterType::ASSIMP_FLAT},
    {"AXYZ", vsr::io::ImporterType::AXYZ},
    {"DLAF", vsr::io::ImporterType::DLAF},
    {"E57XYZ", vsr::io::ImporterType::E57XYZ},
    {"ENSIGHT", vsr::io::ImporterType::ENSIGHT},
    {"GLTF", vsr::io::ImporterType::GLTF},
    {"HDRI", vsr::io::ImporterType::HDRI},
    {"HSMESH", vsr::io::ImporterType::HSMESH},
    {"MINI", vsr::io::ImporterType::MINI},
    {"NBODY", vsr::io::ImporterType::NBODY},
    {"OBJ", vsr::io::ImporterType::OBJ},
    {"PDB", vsr::io::ImporterType::PDB},
    {"PBRT", vsr::io::ImporterType::PBRT},
    {"PLY", vsr::io::ImporterType::PLY},
    {"POINTSBIN_MULTIFILE", vsr::io::ImporterType::POINTSBIN_MULTIFILE},
    {"PT (neural)", vsr::io::ImporterType::PT},
    {"SILO", vsr::io::ImporterType::SILO},
    {"SMESH", vsr::io::ImporterType::SMESH},
    {"SMESH_ANIMATION", vsr::io::ImporterType::SMESH_ANIMATION},
    {"SWC", vsr::io::ImporterType::SWC},
    {"SWC_SDF", vsr::io::ImporterType::SWC_SDF},
    {"TRK", vsr::io::ImporterType::TRK},
    {"USD", vsr::io::ImporterType::USD},
    {"USD_MTLX", vsr::io::ImporterType::USD_MTLX},
    {"USD_MDL", vsr::io::ImporterType::USD_MDL},
    {"VTP", vsr::io::ImporterType::VTP},
    {"VTU", vsr::io::ImporterType::VTU},
    {"XYZDP", vsr::io::ImporterType::XYZDP},
    {"Principled Volume", vsr::io::ImporterType::NONE},
    {"VOLUME", vsr::io::ImporterType::VOLUME},
    {"VOLUME_ANIMATION", vsr::io::ImporterType::VOLUME_ANIMATION},
};

constexpr const char *STAGE_DEFAULT = "(stage default)";

} // namespace

ImportFileDialog::ImportFileDialog(Application *app)
    : Modal(app, "ImportFileDialog")
{}

ImportFileDialog::~ImportFileDialog() = default;

void ImportFileDialog::buildUI()
{
  constexpr int MAX_LENGTH = 2000;
  m_filename.reserve(MAX_LENGTH);

  const char *importerNames[std::size(IMPORTERS)] = {};
  for (size_t i = 0; i < std::size(IMPORTERS); i++)
    importerNames[i] = IMPORTERS[i].name;

  ImGui::BeginDisabled(bool(m_importResult));
  ImGui::Combo("importer type",
      &m_selectedFileType,
      importerNames,
      std::size(importerNames));
  ImGui::EndDisabled();

  if (IMPORTERS[m_selectedFileType].type == vsr::io::ImporterType::NONE) {
    buildPrincipledVolumeUI();
    return;
  }

  static std::string outPath;
  if (ImGui::Button("...")) {
    outPath.clear();
    m_app->getFilenameFromDialog(outPath);
  }

  if (!outPath.empty()) {
    m_filename = outPath;
    outPath.clear();
  }

  ImGui::SameLine();

  auto text_cb = [](ImGuiInputTextCallbackData *cbd) {
    auto &fname = *(std::string *)cbd->UserData;
    fname.resize(cbd->BufTextLen);
    return 0;
  };

  ImGui::InputText("##filename",
      m_filename.data(),
      MAX_LENGTH,
      ImGuiInputTextFlags_CallbackEdit,
      text_cb,
      &m_filename);

  if (vsr::io::isUsdImporter(IMPORTERS[m_selectedFileType].type))
    buildUsdVariantUI();

  //////////

  ImGui::NewLine();

  ImGuiIO &io = ImGui::GetIO();
  if (ImGui::Button("cancel") || ImGui::IsKeyDown(ImGuiKey_Escape))
    this->hide();

  ImGui::SameLine();

  if (ImGui::Button("import")) {
    this->hide();

    auto doLoad = [&]() {
      auto *ctx = appContext();
      auto &scene = ctx->vsr.scene;
      auto *layer = ctx->vsr.scene.defaultLayer();
      auto importRoot = ctx->getFirstSelected();
      if (!importRoot.valid())
        importRoot = layer->root();
      const auto type = IMPORTERS[m_selectedFileType].type;
      // import_file reads Variant Selections from a third ';' field; the
      // layer field between is unused when importing beneath a node.
      auto fileField = m_filename;
      const auto selections =
          vsr::io::usdVariantSelectionsFromString(m_variantSelections);
      if (vsr::io::isUsdImporter(type) && !selections.empty())
        fileField += ";;" + vsr::io::toString(selections);
      vsr::io::ImportFile file{type, fileField};
      vsr::io::import_file(scene, ctx->vsr.animationMgr, file, importRoot);
      scene.signalLayerStructureChanged(layer);
    };

    m_app->showTaskModal(doLoad, "Please Wait: Importing Data...");
  }
}

void ImportFileDialog::buildPrincipledVolumeUI()
{
  if (m_importResult && m_importResult->done.load()) {
    m_importError = m_importResult->error;
    m_importResult.reset();
    if (m_importError.empty()) {
      hide();
      return;
    }
  }
  ImGui::BeginDisabled(bool(m_importResult));
  m_principledEditor.buildUI(m_app);
  const auto validation = m_principledEditor.validationError();
  if (!validation.empty())
    ImGui::TextWrapped("%s", validation.c_str());
  if (!m_importError.empty())
    ImGui::TextWrapped("%s", m_importError.c_str());
  if (ImGui::Button("cancel"))
    hide();
  ImGui::SameLine();
  ImGui::BeginDisabled(!validation.empty() || m_app->blockingTaskRunning());
  if (ImGui::Button("import")) {
    auto *ctx = appContext();
    auto parent = ctx->getFirstSelected();
    if (!parent)
      parent = ctx->vsr.scene.defaultLayer()->root();
    auto options = m_principledEditor.options();
    auto result = std::make_shared<ImportResult>();
    m_importResult = result;
    m_importError.clear();
    m_app->showTaskModal(
        [ctx, parent, options = std::move(options), result]() {
          try {
            vsr::io::import_principledVolume(ctx->vsr.scene, options, parent);
            ctx->vsr.scene.signalLayerStructureChanged(parent->value().layer());
          } catch (const std::exception &e) {
            result->error = e.what();
          }
          result->done.store(true);
        },
        "Please Wait: Importing Principled Volume...");
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}

void ImportFileDialog::buildUsdVariantUI()
{
  ImGui::NewLine();
  ImGui::TextUnformatted("variant selections");

  ImGui::InputText("##variantSelections", &m_variantSelections);
  tooltipForPreviousItem(
      "set=variant,... chosen before the Stage loads, so a variant not "
      "chosen is never read");

  ImGui::SameLine();
  ImGui::BeginDisabled(m_filename.empty());
  if (ImGui::Button("scan stage")) {
    m_scannedVariantSets = vsr::io::usdVariantSets(m_filename);
    m_scannedFile = m_filename;
  }
  ImGui::EndDisabled();
  tooltipForPreviousItem(
      "List the Variant Sets the Stage offers, without loading its payloads");

  // A scan describes one file; once the filename moves on it describes
  // nothing the import will read.
  if (m_scannedFile.empty() || m_scannedFile != m_filename)
    return;

  if (m_scannedVariantSets.empty()) {
    ImGui::TextDisabled("no Variant Sets found");
    return;
  }

  auto selections =
      vsr::io::usdVariantSelectionsFromString(m_variantSelections);
  bool changed = false;
  for (const auto &[set, variants] : m_scannedVariantSets) {
    auto found = selections.find(set);
    const char *preview =
        found == selections.end() ? STAGE_DEFAULT : found->second.c_str();
    if (!ImGui::BeginCombo(set.c_str(), preview))
      continue;
    if (ImGui::Selectable(STAGE_DEFAULT, found == selections.end())) {
      selections.erase(set);
      changed = true;
    }
    for (const auto &variant : variants) {
      // Looked up afresh: the "(stage default)" entry may have just erased
      // the selection `found` pointed at.
      const auto current = selections.find(set);
      const bool isSelected =
          current != selections.end() && current->second == variant;
      if (ImGui::Selectable(variant.c_str(), isSelected)) {
        selections[set] = variant;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  if (changed)
    m_variantSelections = vsr::io::toString(selections);
}

} // namespace vsr::ui::imgui
