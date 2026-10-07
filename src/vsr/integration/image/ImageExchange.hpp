// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_integration
#include "vsr/integration/descriptor/LaunchDescriptor.hpp"
#include "vsr/integration/operations/Operations.hpp"
// std
#include <functional>
#include <memory>
#include <memory_resource>
#include <string>

namespace vsr::integration::edit {
class EditService;
}

namespace vsr::integration::image {

struct ExchangeLimits
{
  std::size_t transientBytes{268435456};
  std::size_t globalRetentionBytes{16u << 20};
  // A host-wide physical lease is acquired once before capture and held
  // through buffer destruction; smaller configured host ceilings also apply.
  std::size_t physicalByteCap{268435456};
  std::function<std::shared_ptr<const void>(std::size_t)> reservePhysicalMemory;
  // Optional host allocator for deterministic allocation/failure control.
  // Must outlive this service and its engine's active jobs.
  std::pmr::memory_resource *allocator{std::pmr::new_delete_resource()};
  // Owning-thread observation of the existing global activity/restoration pool.
  std::function<std::size_t()> availableGlobalRetention;
  std::function<bool(const std::string &, const std::string &)>
      restorationRetained;
};

/*
 * Local POSIX PNG exchange. Capture runs only at an authorized safe point;
 * filesystem scans and codecs run on owned workers using immutable bytes.
 * Export acquires no map, write sequence, render slot or undo payload.
 * Import is opt-in through attachRestoration(); it shares the normal edit
 * store and vsr_restore, retaining exact prior samples before any write.
 * The shipped hosts deliberately have not enabled this unfinished slice.
 * Engine/Integration outlive this service. Destruction joins workers before
 * descriptor-relative cleanup; completed files have no TTL.
 */
class ImageExchange
{
 public:
  ImageExchange(Integration &integration,
      operations::OperationEngine &engine,
      std::string launchId,
      const descriptor::RuntimeDirectory &runtime,
      ExchangeLimits limits = {});
  ~ImageExchange();
  VSR_NOT_COPYABLE(ImageExchange)
  void attachScene(SceneId scene, identity::SceneIdentity &identity);
  // Qualification/continuation seam, not enabled by the shipped hosts yet.
  // Edits and this service share a launch and owning-thread lifetime.
  // Import remains unavailable without the host's physical reservation and
  // authoritative global retention/availability callbacks in ExchangeLimits.
  void attachRestoration(edit::EditService &edits);
  bool importAvailable() const;
  // Empty session selects launch-wide descriptor metadata, not a ledger.
  std::string metadata(SceneId scene, const std::string &sessionId = {}) const;
  std::string unavailableReason(SceneId scene) const;
  // Owning-thread host loop: release completed backend/cancellation slots.
  void pump();
  operations::Admission submitExport(const session::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene,
      identity::ObjectHandle handle);
  operations::Admission submitImport(const session::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene,
      identity::ObjectHandle handle,
      std::string dataVersion,
      std::string fileName);

 private:
  struct State;
  std::shared_ptr<State> m_state;
  Integration *m_integration{nullptr};
  operations::OperationEngine *m_engine{nullptr};
};

} // namespace vsr::integration::image
