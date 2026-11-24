module;

#include <cassert>
#include <cstddef>
#include <string_view>

export module arche:domain.snapshot_impl;

import :domain.snapshot;
import :snapshot;

export namespace arche {

using namespace internal;

// =============================================================================
// SnapshotFacade Method Implementations
// =============================================================================

size_t SnapshotFacade::size_bytes() const noexcept {
  return Snapshot::size_bytes(view());
}

SnapshotHeader SnapshotFacade::header() const noexcept {
  return Snapshot::header(view());
}

size_t SnapshotFacade::save(void *destination_buffer, size_t max_bytes) {
  return Snapshot::save(
      view(),
      SnapshotTarget{destination_buffer, max_bytes}
  );
}

void SnapshotFacade::load(const void *snapshot_buffer, size_t bytes) {
  Snapshot::load(
      view(),
      SnapshotSource{snapshot_buffer, bytes}
  );
}

bool SnapshotFacade::save_to_file(std::string_view file_path) {
  return Snapshot::save_to_file(view(), file_path);
}

bool SnapshotFacade::load_from_file(std::string_view file_path) {
  return Snapshot::load_from_file(view(), file_path);
}

} // namespace arche
