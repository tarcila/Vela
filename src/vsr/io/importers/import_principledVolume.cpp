// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/PrincipledVolumeImport.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_io
#include "vsr/io/archives/ObjectArchive.hpp"
#include "vsr/io/importers.hpp"
#include "vsr/io/importers/detail/PrincipledVolumeField.hpp"
// std
#include <stdexcept>

namespace vsr::io {

vsr::scene::VolumeRef import_principledVolume(vsr::scene::Scene &scene,
    const PrincipledVolumeImportOptions &options,
    vsr::scene::LayerNodeRef location)
{
  using namespace vsr::scene;
  validatePrincipledVolumeOptions(options);
  // Readers may allocate objects before reporting failure. Stage the entire
  // import away from the destination; Object Archives transfer the closure
  // atomically, without collecting or modifying unrelated scene objects.
  Scene staging;
  auto volume = staging.createObject<Volume>(tokens::volume::principledVolume);
  volume->setName(options.name.c_str());
  std::optional<vsr::math::box3> bounds = options.bounds;
  for (const auto &[property, source] : options.fields) {
    try {
      if (!std::filesystem::is_regular_file(source.file))
        throw std::runtime_error("not a readable regular file");
      auto field = import_spatial_field(staging, source.file.string().c_str());
      if (!field)
        throw std::runtime_error("field reader failed");
      const auto info = detail::inspectPrincipledVolumeField(*field);
      const bool wantsRGB =
          property == "emission" || property == "scatteringColor";
      if (info.rgb != wantsRGB)
        throw std::runtime_error(
            wantsRGB ? "requires an RGB field" : "requires a scalar field");
      if (!options.bounds) {
        if (!info.bounds)
          throw std::runtime_error(
              "cannot derive a finite sampling domain; specify bounds explicitly");
        if (!bounds)
          bounds = info.bounds;
        else
          for (int i = 0; i < 3; ++i) {
            bounds->lower[i] =
                std::min(bounds->lower[i], info.bounds->lower[i]);
            bounds->upper[i] =
                std::max(bounds->upper[i], info.bounds->upper[i]);
          }
      }
      volume->setParameterObject((property + ".field").c_str(), *field);
    } catch (const std::exception &e) {
      throw std::runtime_error("principled volume '" + options.name + "', "
          + property + " (" + source.file.string() + "): " + e.what());
    }
  }
  if (!bounds)
    throw std::runtime_error(
        "principled volume: constant-only media require explicit bounds");
  volume->setParameter("bounds", *bounds);
  for (const auto &[name, value] : options.parameters)
    std::visit(
        [&](const auto &v) { volume->setParameter(name.c_str(), v); }, value);

  vsr::core::DataTree archive;
  if (!serialize_ObjectArchive(*volume, archive.root()))
    throw std::runtime_error(
        "principled volume: could not serialize imported medium");
  auto *object = deserialize_ObjectArchive(scene, archive.root());
  if (!object)
    throw std::runtime_error(
        "principled volume: could not transfer imported medium");
  auto result = scene.getObject<Volume>(object->index());
  scene.insertChildObjectNode(
      location ? location : scene.defaultLayer()->root(), result);
  return result;
}

} // namespace vsr::io
