// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/frame/FrameObservation.hpp"
// std
#include <algorithm>
#include <array>
#include <string_view>
#include <thread>

namespace vsr::integration::frame {

// Host glue, instantiated by a viewer, without an integration -> UI dependency.
// Connect/disconnect on the owning thread outside device setup. The pass's
// provider rejects any off-owner invocation before reading live state.
// Qualification/exposure belongs to FrameService, never to this connection.
struct ViewerFrameMetrics
{
  std::size_t starts{0}, acquisitions{0}, submissions{0};
  double maxStartMs{0.}, maxAcquireMs{0.}, maxSubmitMs{0.};
};

template <typename Viewport>
class ViewerFrameSource
{
 public:
  ViewerFrameSource(FrameService &frames,
      ViewId view,
      identity::SceneIdentity &scene,
      Viewport &viewport);
  ~ViewerFrameSource();
  VSR_NOT_COPYABLE(ViewerFrameSource)
  VSR_NOT_MOVEABLE(ViewerFrameSource)
  bool connected() const;
  ViewerFrameMetrics metrics() const;
  ViewId viewId() const;
  Viewport *viewport() const;
  void disconnect();

 private:
  struct Control
  {
    std::atomic<bool> active{true};
    std::shared_ptr<const void> reservation;
  };
  EffectiveViewBinding binding() const;
  FrameService *m_frames;
  ViewId m_view;
  identity::SceneIdentity *m_scene;
  Viewport *m_viewport;
  const std::thread::id m_owner{std::this_thread::get_id()};
  std::shared_ptr<Control> m_alive;
  std::array<std::weak_ptr<const Production>, 3> m_productions;
  std::size_t m_nextProduction{0};
  ViewerFrameMetrics m_metrics;
};

// Inlined definitions ////////////////////////////////////////////////////////

template <typename Viewport>
ViewerFrameSource<Viewport>::ViewerFrameSource(FrameService &frames,
    ViewId view,
    identity::SceneIdentity &scene,
    Viewport &viewport)
    : m_frames(&frames), m_view(view), m_scene(&scene), m_viewport(&viewport)
{
  try {
    auto lease = m_frames->reserveViewerConnection();
    if (!lease)
      return;
    m_alive = std::make_shared<Control>();
    m_alive->reservation = std::move(lease);
    const auto alive = m_alive;
    const auto owner = m_owner;
    viewport.setFrameTeardown([this, alive, owner] {
      if (std::this_thread::get_id() == owner && alive->active.load())
        disconnect();
    });
    viewport.setFrameStateProvider(
        [this, alive, owner]() -> std::shared_ptr<const void> {
          if (std::this_thread::get_id() != owner || !alive->active.load())
            return {};
          const auto start = std::chrono::steady_clock::now();
          auto production = m_frames->beginViewerProduction(
              m_view, [this] { return binding(); });
          m_metrics.maxStartMs = std::max(m_metrics.maxStartMs,
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - start)
                  .count());
          if (production)
            ++m_metrics.starts;
          m_productions[m_nextProduction++ % m_productions.size()] = production;
          return production;
        });
    viewport.setFrameObservation(
        [this, alive, owner](const std::uint32_t *pixels,
            std::uint32_t width,
            std::uint32_t height,
            const char *path,
            std::shared_ptr<const void> start) -> std::shared_ptr<const void> {
          if (std::this_thread::get_id() != owner || !alive->active.load())
            return {};
          // Replacing a provider cannot reinterpret an older provider's opaque
          // stamp. Only a bounded set of receipts issued by this connection is
          // accepted; an unusually old completed render stays unavailable.
          std::shared_ptr<const Production> matched;
          for (auto &issued : m_productions)
            if (auto candidate = issued.lock();
                candidate && candidate.get() == start.get())
              matched = std::move(candidate);
          const auto before = std::chrono::steady_clock::now();
          auto acquired = m_frames->acquireViewerPixels(
              m_view,
              std::move(matched),
              [this] { return binding(); },
              pixels,
              width,
              height,
              path ? std::string_view(path) : std::string_view{});
          m_metrics.maxAcquireMs = std::max(m_metrics.maxAcquireMs,
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - before)
                  .count());
          if (acquired)
            ++m_metrics.acquisitions;
          return acquired;
        },
        [this, alive, owner](
            auto presentation, std::shared_ptr<const void> acquired) {
          if (std::this_thread::get_id() != owner || !alive->active.load())
            return;
          if (!presentation) {
            m_frames->retireViewer(m_view);
            return;
          }
          const auto before = std::chrono::steady_clock::now();
          m_frames->presentViewerPixels(m_view,
              std::static_pointer_cast<const ViewerAcquisition>(acquired),
              presentation->width,
              presentation->height,
              presentation->displayWidth,
              presentation->displayHeight,
              presentation->submittedAt,
              presentation->framebufferScaleX,
              presentation->framebufferScaleY);
          m_metrics.maxSubmitMs = std::max(m_metrics.maxSubmitMs,
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - before)
                  .count());
          ++m_metrics.submissions;
        });
  } catch (const std::bad_alloc &) {
    if (m_alive)
      m_alive->active.store(false);
    m_viewport->setFrameObservation({}, {});
    m_viewport->setFrameStateProvider({});
    m_viewport->setFrameTeardown({});
  }
}

template <typename Viewport>
bool ViewerFrameSource<Viewport>::connected() const
{
  return m_alive && m_alive->active.load();
}

template <typename Viewport>
ViewerFrameMetrics ViewerFrameSource<Viewport>::metrics() const
{
  return std::this_thread::get_id() == m_owner ? m_metrics
                                               : ViewerFrameMetrics{};
}

template <typename Viewport>
ViewerFrameSource<Viewport>::~ViewerFrameSource()
{
  disconnect();
}

template <typename Viewport>
ViewId ViewerFrameSource<Viewport>::viewId() const
{
  return m_view;
}

template <typename Viewport>
Viewport *ViewerFrameSource<Viewport>::viewport() const
{
  return m_viewport;
}

template <typename Viewport>
void ViewerFrameSource<Viewport>::disconnect()
{
  if (std::this_thread::get_id() != m_owner || !m_viewport)
    return;
  if (!m_alive) {
    m_viewport = nullptr; // reservation failed before installing callbacks
    return;
  }
  m_alive->active.store(false);
  m_viewport->setFrameObservation({}, {});
  m_viewport->setFrameStateProvider({});
  m_viewport->setFrameTeardown({});
  m_viewport = nullptr;
  m_frames->retireViewer(m_view);
}

template <typename Viewport>
EffectiveViewBinding ViewerFrameSource<Viewport>::binding() const
{
  EffectiveViewBinding value;
  static_cast<rendering::EffectiveView &>(value) = m_viewport->effectiveView();
  if (auto camera = m_viewport->boundCamera())
    value.cameraHandle = m_scene->handleOf(camera.data());
  if (auto renderer = m_viewport->boundRenderer())
    value.rendererHandle = m_scene->handleOf(renderer.data());
  return value;
}

} // namespace vsr::integration::frame
