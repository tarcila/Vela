// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/mesh/MeshReplacement.hpp"
// vsr_scene
#include "vsr/scene/detail/PreparedSurfaceStorage.hpp"
// std
#include <memory>

namespace vsr::integration::content::detail {

struct SurfaceGraphSettings
{
  core::math::float3 color{0.8f};
  core::math::mat4 transform{core::math::IDENTITY_MAT4};
};

/* Private preparation only, NOT a live-scene adoption or admitted operation.
 * The owning Scene has no external delegates, borrowed objects or live handles.
 * A future publication transaction must reserve/profile pool, identity, result
 * and recovery storage and remap this closure; ordinary copying into a live
 * Scene is not atomic adoption. No payload pin into a live Scene is retained.
 * Only owned opaque matte appearance is prepared here. Borrowed-material
 * authority/lifetime validation belongs to the eventual live transaction. */
class PreparedSurfaceGraph
{
 public:
  PreparedSurfaceGraph();
  VSR_NOT_COPYABLE(PreparedSurfaceGraph)
  VSR_NOT_MOVEABLE(PreparedSurfaceGraph)
  const scene::Scene &scene() const;
  const scene::Geometry &geometry() const;
  const scene::Material &material() const;
  const scene::Surface &surface() const;
  scene::LayerNodeRef root() const;

 private:
  friend const char *prepareSurfaceGraph(std::pmr::vector<std::uint8_t> &&,
      const char *,
      const SurfaceGraphSettings &,
      std::unique_ptr<PreparedSurfaceGraph> &,
      const std::function<bool()> &);
  scene::Scene m_scene;
  scene::GeometryRef m_geometry;
  scene::MaterialRef m_material;
  scene::SurfaceRef m_surface;
  scene::LayerNodeRef m_root;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline PreparedSurfaceGraph::PreparedSurfaceGraph()
{
  // Ordinary Scene defaults are not part of the prepared closure.
  m_scene.removeAllObjects();
}

inline const scene::Scene &PreparedSurfaceGraph::scene() const
{
  return m_scene;
}

inline const scene::Geometry &PreparedSurfaceGraph::geometry() const
{
  return *m_geometry;
}

inline const scene::Material &PreparedSurfaceGraph::material() const
{
  return *m_material;
}

inline const scene::Surface &PreparedSurfaceGraph::surface() const
{
  return *m_surface;
}

inline scene::LayerNodeRef PreparedSurfaceGraph::root() const
{
  return m_root;
}

/* Consumes a bounded typed carrier, never a filename or target identity.
 * Preserve and replace both mean new data here: there is no previous Geometry.
 * Omitted/remove fields are absent. Unreferenced payloads reject rather than
 * silently discard authored data. Payload IDs preserve aliases, not byte dedup.
 * Failure/cancellation leaves the caller's previous prepared graph untouched.
 * The caller owns processing admission; this function is not a budget grant. */
inline const char *prepareSurfaceGraph(std::pmr::vector<std::uint8_t> &&bytes,
    const char *rootName,
    const SurfaceGraphSettings &settings,
    std::unique_ptr<PreparedSurfaceGraph> &result,
    const std::function<bool()> &abandoned = {})
{
  if (!rootName || !*rootName || std::strlen(rootName) > 256
      || !mesh::detail::utf8(rootName))
    return "invalid_content_root_name";
  for (size_t i = 0; i < 3; ++i)
    if (!std::isfinite(settings.color[i]) || settings.color[i] < 0.f
        || settings.color[i] > 1.f)
      return "invalid_matte_color";
  for (size_t c = 0; c < 4; ++c)
    for (size_t r = 0; r < 4; ++r)
      if (!std::isfinite(settings.transform[c][r]))
        return "invalid_surface_transform";
  if (settings.transform[0][3] != 0.f || settings.transform[1][3] != 0.f
      || settings.transform[2][3] != 0.f || settings.transform[3][3] != 1.f)
    return "invalid_surface_transform";
  mesh::Bundle bundle;
  if (const auto *reason =
          mesh::decodeBundle(std::move(bytes), bundle, abandoned))
    return reason;
  for (auto &field : bundle.fields)
    if (field.action == mesh::Action::PRESERVE)
      field.action = mesh::Action::REPLACE;
  mesh::MeshReplacement plan;
  const scene::Geometry empty(scene::tokens::geometry::triangle);
  if (const auto *reason = mesh::prepareMesh(empty, bundle, plan, true))
    return reason;
  for (size_t i = 0; i < bundle.arrays.size(); ++i)
    if (!plan.needed[i])
      return "unreferenced_mesh_payload";
  if (abandoned && abandoned())
    return "cancelled";
  auto staged = std::make_unique<PreparedSurfaceGraph>();
  auto &scene = staged->m_scene;
  scene::detail::PreparedSurfaceStorage::reserve(scene, bundle.arrays.size());
  std::array<scene::ArrayRef, mesh::FIELD_CAP> arrays{};
  for (size_t i = 0; i < bundle.arrays.size(); ++i) {
    if (abandoned && abandoned())
      return "cancelled";
    const auto &payload = bundle.arrays[i];
    arrays[i] = scene.createArray(payload.elementType, payload.count);
    arrays[i]->setData(static_cast<const void *>(bundle.data(payload)));
  }
  staged->m_geometry =
      scene.createObject<scene::Geometry>(scene::tokens::geometry::triangle);
  for (const auto &field : plan.fields) {
    staged->m_geometry->setParameterObject(
        field.name.c_str(), *arrays[field.payload]);
    staged->m_geometry->parameter(field.name.c_str())
        ->setEnabled(field.enabled);
  }
  staged->m_material =
      scene.createObject<scene::Material>(scene::tokens::material::matte);
  staged->m_material->setParameter("color", settings.color);
  staged->m_material->setParameter("opacity", 1.f);
  staged->m_material->setParameter("alphaMode", "opaque");
  staged->m_material->parameter("alphaMode")->setStringSelection(0);
  staged->m_surface =
      scene.createSurface("Surface", staged->m_geometry, staged->m_material);
  staged->m_root = scene.insertChildTransformNode(
      scene.defaultLayer()->root(), settings.transform, rootName);
  scene.insertChildObjectNode(staged->m_root, staged->m_surface, "Surface");
  // Exercise the same full structural profile used by export/inspection.
  mesh::CapturePlan captured;
  if (const auto *reason = mesh::plan(*staged->m_geometry, captured))
    return reason;
  if (abandoned && abandoned())
    return "cancelled";
  result = std::move(staged);
  return nullptr;
}

} // namespace vsr::integration::content::detail
