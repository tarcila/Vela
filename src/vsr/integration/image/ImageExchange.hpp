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

namespace vsr::integration::activity {
class ActivityLog;
}

namespace vsr::integration::edit {
class EditService;
}

namespace vsr::integration::restore {
class RestoreService;
}

namespace vsr::integration::image {

struct ExchangeLimits
{
  std::size_t transientBytes{268435456};
  // Launch configuration hint for export-only hosts. Qualified Import reads
  // policy/accounting directly from retention, never from a second ledger cap.
  std::uint64_t globalRetentionBytes{UINT64_C(4294967296)};
  std::uint64_t hardRetentionBytes{UINT64_C(8589934592)};
  const activity::ActivityLog *retention{nullptr};
  // A host-wide physical lease is acquired once before capture and held
  // through buffer destruction; smaller configured host ceilings also apply.
  std::size_t physicalByteCap{268435456};
  std::function<std::shared_ptr<const void>(std::size_t)> reservePhysicalMemory;
  // Optional host allocator for deterministic allocation/failure control.
  // Must outlive this service and its engine's active jobs.
  std::pmr::memory_resource *allocator{std::pmr::new_delete_resource()};
  std::function<bool(const std::string &, const std::string &)>
      restorationRetained;
};

/*
 * Local POSIX PNG exchange. Capture runs only at an authorized safe point;
 * filesystem scans and codecs run on owned workers using immutable bytes.
 * Export acquires no map, write sequence, render slot or undo payload.
 * Import is opt-in through attachRestoration(); it shares the normal edit
 * store and vsr_restore, retaining exact prior samples before any write.
 * Qualified hosts bind the same physical pool and Activity retention policy.
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
  // Bind normal recovery before publishing Import tools or capabilities.
  // Edits and this service share a launch and owning-thread lifetime.
  // Import remains unavailable without the host's physical reservation and
  // authoritative global retention/availability callbacks in ExchangeLimits.
  void attachRestoration(
      edit::EditService &edits, restore::RestoreService &restores);
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
  operations::Admission submitMeshExport(
      const session::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene,
      identity::ObjectHandle handle);
  operations::Admission submitMeshImport(
      const session::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene,
      identity::ObjectHandle handle,
      std::string targetVersion,
      std::string dependencies,
      std::string fileName,
      std::optional<std::uint64_t> approvalRequestId = {});
  operations::Admission submitImport(const session::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene,
      identity::ObjectHandle handle,
      std::string dataVersion,
      std::string fileName,
      std::optional<std::uint64_t> approvalRequestId = {});

 private:
  operations::Admission submitCapture(const session::Credentials &credentials,
      const operations::OperationRef &ref,
      SceneId scene,
      identity::ObjectHandle handle,
      bool mesh);
  struct State;
  std::shared_ptr<State> m_state;
  Integration *m_integration{nullptr};
  operations::OperationEngine *m_engine{nullptr};
};

} // namespace vsr::integration::image
