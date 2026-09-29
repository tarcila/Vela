// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/pipeline/saveImage.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// stb_image
#include "stb_image_write.h"

namespace vsr::rendering {

bool saveImage(const ImagePipeline &pipeline, const std::string &filename)
{
  const auto size = pipeline.dimensions();
  const uint32_t *pixels = pipeline.getColorBuffer();
  if (!pixels || size.x == 0 || size.y == 0) {
    vsr::core::logError(
        "[saveImage] nothing rendered, not writing '%s'", filename.c_str());
    return false;
  }

  // The color buffer is in ANARI row order (bottom row first). stb's flip
  // flag is process-global state, so set it explicitly for this write and
  // reset it to stb's default afterwards.
  stbi_flip_vertically_on_write(1);
  const int ok = stbi_write_png(
      filename.c_str(), int(size.x), int(size.y), 4, pixels, int(size.x) * 4);
  stbi_flip_vertically_on_write(0);

  if (!ok) {
    vsr::core::logError("[saveImage] failed to write '%s'", filename.c_str());
    return false;
  }
  vsr::core::logStatus("[saveImage] saved '%s'", filename.c_str());
  return true;
}

} // namespace vsr::rendering
