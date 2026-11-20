// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"
#include "PrincipledVolumeImportEditor.h"
#include <atomic>
#include <memory>
// vsr_io
#include "vsr/io/UsdImport.hpp"
// std
#include <string>

namespace vsr::ui::imgui {

struct ImportFileDialog : public Modal
{
  ImportFileDialog(Application *app);
  ~ImportFileDialog() override;

  void buildUI() override;

 private:
  void buildUsdVariantUI();
  void buildPrincipledVolumeUI();

  struct ImportResult
  {
    std::atomic<bool> done{false};
    std::string error;
  };
  PrincipledVolumeImportEditor m_principledEditor;
  std::shared_ptr<ImportResult> m_importResult;
  std::string m_importError;

  std::string m_filename;
  int m_selectedFileType{0};

  // USD only. The text, "set=variant,...", is what the import uses; the
  // per-set combos a scan produces edit it, so a set the scan cannot see (one
  // only a payload authors) can still be typed in.
  std::string m_variantSelections;
  vsr::io::UsdVariantSetCatalog m_scannedVariantSets;
  std::string m_scannedFile;
};

} // namespace vsr::ui::imgui
