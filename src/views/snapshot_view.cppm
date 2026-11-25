module;

#include <cassert>
#include <cstddef>
#include <string_view>

export module arche:snapshot_view;

import :snapshot;

export namespace arche {

using SnapshotStorageView = internal::SnapshotStorageView;

// =============================================================================
// Snapshots Domain View
// =============================================================================

/**
 * @class SnapshotView
 * @brief Domain view for full-world binary serialization and persistence.
 *
 * Accessible via `world.snapshots`.
 */
class SnapshotView {
private:
  friend class World;
  explicit constexpr SnapshotView(
      internal::SnapshotStorageView (*get_view)(void *ctx),
      void *ctx
  ) noexcept
      : get_view_(get_view), ctx_(ctx) {}

public:
  SnapshotView(const SnapshotView &) noexcept = default;
  SnapshotView &operator=(const SnapshotView &) noexcept = default;
  SnapshotView(SnapshotView &&) noexcept = default;
  SnapshotView &operator=(SnapshotView &&) noexcept = default;
  ~SnapshotView() = default;

  /// Serializes all archetypes, chunk columns, and active entities into a destination buffer.
  size_t save(void *destination_buffer, size_t max_bytes) {
    return internal::Snapshot::save(
        view(),
        internal::SnapshotTarget{destination_buffer, max_bytes}
    );
  }

  /// Restores complete simulation state from a serialized snapshot buffer.
  void load(const void *snapshot_buffer, size_t bytes) {
    internal::Snapshot::load(
        view(),
        internal::SnapshotSource{snapshot_buffer, bytes}
    );
  }

  /// Saves the complete simulation state directly to a file on disk.
  bool save_to_file(std::string_view file_path) {
    return internal::Snapshot::save_to_file(view(), file_path);
  }

  /// Restores the complete simulation state directly from a file on disk.
  bool load_from_file(std::string_view file_path) {
    return internal::Snapshot::load_from_file(view(), file_path);
  }

  /// Returns the total buffer size in bytes required to store a snapshot of current state.
  [[nodiscard]] size_t size_bytes() const noexcept {
    return internal::Snapshot::size_bytes(view());
  }

  /// Returns the snapshot header containing magic numbers, version, and allocation counts.
  [[nodiscard]] SnapshotHeader header() const noexcept {
    return internal::Snapshot::header(view());
  }

private:
  [[nodiscard]] internal::SnapshotStorageView view() const noexcept {
    assert(get_view_ != nullptr);
    return get_view_(ctx_);
  }

  internal::SnapshotStorageView (*get_view_)(void *ctx){nullptr};
  void *ctx_{nullptr};
};

using SnapshotFacade = SnapshotView;

} // namespace arche
