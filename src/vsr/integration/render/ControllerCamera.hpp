// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/render/RevisionRender.hpp"
// vsr_rendering
#include "vsr/rendering/view/Manipulator.hpp"
// std
#include <limits>

namespace vsr::integration::render {

/*
 * Owning-thread adapter for an actual viewport controller and bound Scene
 * camera. Recovery has an internal role, not a user pose name, and never owns a
 * camera. All allocations and parameter-batch capacity are prepared before
 * installation. The host supplies lifetime/binding checks and lists this slot
 * in Camera Poses.
 */
class ControllerCamera : public CameraController
{
 public:
  ControllerCamera(identity::SceneIdentity &identity,
      identity::ViewRevisionTracker &tracker,
      rendering::Manipulator &controller,
      std::function<scene::Camera *()> boundCamera,
      std::function<bool()> isLive);
  bool hasRecovery() const;
  bool recoveryAvailable() const;
  void setRecoveryByteLimit(std::size_t bytes);
  Outcome prepare(
      const CameraRequest &request, PreparedCameraChange &change) override;
  Outcome restore();

 private:
  struct ParameterState
  {
    core::Token name;
    core::Any value;
    bool enabled{true};
  };
  struct Snapshot
  {
    rendering::Manipulator controller;
    identity::ObjectHandle camera;
    core::Token projection;
    std::vector<ParameterState> parameters;
  };
  identity::SceneIdentity *m_identity{nullptr};
  identity::ViewRevisionTracker *m_tracker{nullptr};
  rendering::Manipulator *m_controller{nullptr};
  std::function<scene::Camera *()> m_boundCamera;
  std::function<bool()> m_isLive;
  std::optional<Snapshot> m_recovery;
  std::size_t m_recoveryByteLimit{64u << 10};
};

inline ControllerCamera::ControllerCamera(identity::SceneIdentity &identity,
    identity::ViewRevisionTracker &tracker,
    rendering::Manipulator &controller,
    std::function<scene::Camera *()> boundCamera,
    std::function<bool()> isLive)
    : m_identity(&identity),
      m_tracker(&tracker),
      m_controller(&controller),
      m_boundCamera(std::move(boundCamera)),
      m_isLive(std::move(isLive))
{}

inline bool ControllerCamera::hasRecovery() const
{
  return m_recovery.has_value();
}

inline bool ControllerCamera::recoveryAvailable() const
{
  if (!m_recovery || !m_isLive())
    return false;
  auto *camera = m_boundCamera();
  return camera && m_identity->resolve(m_recovery->camera) == camera
      && camera->subtype() == m_recovery->projection;
}

inline void ControllerCamera::setRecoveryByteLimit(std::size_t bytes)
{
  m_recoveryByteLimit = bytes;
}

inline Outcome ControllerCamera::prepare(
    const CameraRequest &request, PreparedCameraChange &change)
{
  auto reject = [&](Outcome outcome, const char *reason) {
    change.reason = reason;
    return outcome;
  };
  auto *camera = m_isLive() ? m_boundCamera() : nullptr;
  if (!camera || !validCameraPose(request))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  const bool perspective =
      camera->subtype() == scene::tokens::camera::perspective;
  const bool orthographic =
      camera->subtype() == scene::tokens::camera::orthographic;
  if ((!perspective && !orthographic)
      || (orthographic && request.fovyDegrees != 0.))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  for (double value : request.position)
    if (std::abs(value) > std::numeric_limits<float>::max())
      return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  const auto direction = normalizedCameraVector(request.direction);
  const auto up = normalizedCameraVector(request.up);
  auto vector = [](const std::array<double, 3> &v) {
    return vsr::math::float3(float(v[0]), float(v[1]), float(v[2]));
  };
  auto next = *m_controller;
  if (perspective)
    next.setPose(vector(request.position), vector(direction), vector(up));
  else
    next.setFixedDistancePose(vector(request.position),
        vector(direction),
        vector(up),
        next.distance());
  const auto eye = perspective ? next.eye() : next.eye_FixedDistance();
  const auto dir = next.dir();
  const auto effectiveUp = next.up();
  for (int i = 0; i < 3; ++i)
    if (!std::isfinite(eye[i]) || !std::isfinite(dir[i])
        || !std::isfinite(effectiveUp[i]))
      return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  CameraRequest effectiveRequest;
  effectiveRequest.position = {eye.x, eye.y, eye.z};
  effectiveRequest.direction = {dir.x, dir.y, dir.z};
  effectiveRequest.up = {effectiveUp.x, effectiveUp.y, effectiveUp.z};
  if (!validCameraPose(effectiveRequest))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  auto fovy = camera->parameterValueAs<float>("fovy");
  if (perspective && (!fovy || !std::isfinite(*fovy)))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  std::size_t bytes = sizeof(Snapshot);
  for (std::size_t i = 0; i < camera->numParameters(); ++i) {
    const auto &value = camera->parameterAt(i).value();
    if (value.holdsObject())
      return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
    bytes += sizeof(ParameterState);
    if (value.type() == ANARI_STRING)
      bytes += value.getStringView().size();
    if (bytes > m_recoveryByteLimit)
      return reject(Outcome::BUSY, "resource_limit");
  }
  if (bytes > m_recoveryByteLimit)
    return reject(Outcome::BUSY, "resource_limit");
  Snapshot before;
  before.controller = *m_controller;
  before.camera = m_identity->handleOf(camera);
  before.projection = camera->subtype();
  before.parameters.reserve(camera->numParameters());
  for (std::size_t i = 0; i < camera->numParameters(); ++i) {
    const auto &p = camera->parameterAt(i);
    before.parameters.push_back({p.name(), p.value(), p.isEnabled()});
  }
  // Input immediately before this safe point may not yet have flushed the
  // controller's pose to the Scene camera. Recovery captures that input too.
  for (auto &parameter : before.parameters) {
    if (parameter.name == core::Token("position"))
      parameter.value =
          perspective ? m_controller->eye() : m_controller->eye_FixedDistance();
    else if (parameter.name == core::Token("direction"))
      parameter.value = m_controller->dir();
    else if (parameter.name == core::Token("up"))
      parameter.value = m_controller->up();
  }
  std::vector<std::pair<scene::Parameter *, core::Any>> writes;
  auto write = [&](core::Token name, core::Any value) {
    auto *p = camera->parameter(name);
    if (!p || p->value().type() != value.type())
      return false;
    writes.emplace_back(p, std::move(value));
    return true;
  };
  if (!write("position", eye) || !write("direction", dir)
      || !write("up", effectiveUp))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  const float appliedFovy = request.fovyDegrees == 0.
      ? fovy.value_or(0.f)
      : vsr::math::radians(float(request.fovyDegrees));
  if (perspective
      && (!(appliedFovy > 0.f) || !(vsr::math::degrees(appliedFovy) < 180.f)))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  if (perspective && request.fovyDegrees != 0. && !write("fovy", appliedFovy))
    return reject(Outcome::INVALID_REQUEST, "unsupported_view_camera");
  camera->reserveParameterBatch(writes.size());
  change.effective.projection = camera->subtype().str();
  change.effective.position = {eye.x, eye.y, eye.z};
  change.effective.direction = normalizedCameraVector({dir.x, dir.y, dir.z});
  change.effective.up =
      normalizedCameraVector({effectiveUp.x, effectiveUp.y, effectiveUp.z});
  change.effective.fovyDegrees =
      perspective ? vsr::math::degrees(appliedFovy) : 0.;
  change.install = [this,
                       camera,
                       next,
                       before = std::move(before),
                       writes = std::move(writes)]() mutable {
    camera->beginParameterBatch();
    for (auto &write : writes)
      write.first->setPreparedValue(std::move(write.second));
    m_recovery = std::move(before);
    m_controller->setState(next);
  };
  const auto handle = m_identity->handleOf(camera);
  change.isCurrent = [this, camera, handle] {
    return m_isLive() && m_boundCamera() == camera
        && m_identity->resolve(handle) == camera;
  };
  change.synchronize = [camera] { camera->endParameterBatch(); };
  return Outcome::OK;
}

inline Outcome ControllerCamera::restore()
{
  bool committed = false;
  // Host callback boundary: allocation failures precede installation;
  // notification failures must not imply rollback of the restored view.
  try {
    if (!recoveryAvailable())
      return Outcome::OUTCOME_UNAVAILABLE;
    auto *camera = m_boundCamera();
    auto writes = m_recovery->parameters;
    for (const auto &write : writes) {
      const auto *parameter = camera->parameter(write.name);
      if (!parameter || parameter->value().type() != write.value.type())
        return Outcome::OUTCOME_UNAVAILABLE;
    }
    // Value and enabled-state changes each enqueue a notification. Reserve
    // both before mutation; endParameterBatch coalesces them for consumers.
    camera->reserveParameterBatch(2 * writes.size());
    camera->beginParameterBatch();
    for (auto &write : writes) {
      camera->parameter(write.name)
          ->setPreparedValue(std::move(write.value))
          .setEnabled(write.enabled);
    }
    m_controller->setState(m_recovery->controller);
    m_tracker->notePipelineChanged();
    committed = true;
    camera->endParameterBatch();
  } catch (...) {
    return committed ? Outcome::APPLIED_WITH_SYNC_FAILURE : Outcome::BUSY;
  }
  return Outcome::OK;
}

} // namespace vsr::integration::render
