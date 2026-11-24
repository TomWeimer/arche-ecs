module;

#include <cassert>
#include <cstddef>
#include <string_view>

export module arche:facade.snapshot;

import :types;
import :snapshot;

export namespace arche {

// =============================================================================
// Snapshots Domain Sub-Facade
// =============================================================================

/**
 * @class SnapshotFacade
 * @brief Domain sub-facade for full-world binary serialization and persistence.
 *
 * Serializes active chunk memory pages, SlotMap generation tables, and
 * archetype schemas directly into contiguous memory buffers or disk checkpoint
 * files via the Snapshot engine contract.
 *
 * Accessible via `world.snapshots`.
 */
class SnapshotFacade {
public:
  constexpr SnapshotFacade() noexcept = default;
  explicit constexpr SnapshotFacade(
      SnapshotStorageView (*get_view)(void *ctx),
      void *ctx
  ) noexcept
      : get_view_(get_view), ctx_(ctx) {}

  /// Serializes all archetypes, chunk columns, and active entities into a destination buffer.
  size_t save(void *destination_buffer, size_t max_bytes);

  /// Restores complete simulation state from a serialized snapshot buffer.
  void load(const void *snapshot_buffer, size_t bytes);

  /// Saves the complete simulation state directly to a file on disk.
  bool save_to_file(std::string_view file_path);

  /// Restores the complete simulation state directly from a file on disk.
  bool load_from_file(std::string_view file_path);

  /// Returns the total buffer size in bytes required to store a snapshot of current state.
  [[nodiscard]] size_t size_bytes() const noexcept;

  /// Returns the snapshot header containing magic numbers, version, and allocation counts.
  [[nodiscard]] SnapshotHeader header() const noexcept;

private:
  [[nodiscard]] SnapshotStorageView view() const noexcept {
    assert(get_view_ != nullptr);
    return get_view_(ctx_);
  }

  SnapshotStorageView (*get_view_)(void *ctx){nullptr};
  void *ctx_{nullptr};
};

} // namespace arche
