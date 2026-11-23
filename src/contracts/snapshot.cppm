module;

#include <cstddef>
#include <string_view>

export module arche:contracts.snapshot;

import :types.detail.snapshot_header;

export namespace arche {

class World;

// =============================================================================
// Snapshots Domain Sub-Facade
// =============================================================================

/**
 * @class SnapshotFacade
 * @brief Domain sub-facade for full-world binary serialization and persistence.
 *
 * Serializes active chunk memory pages, SlotMap generation tables, and
 * archetype schemas directly into contiguous memory buffers or disk checkpoint
 * files with zero per-entity overhead.
 *
 * Accessible via `world.snapshots`.
 *
 * ```cpp
 * // Example:
 * world.snapshots.save_to_file("saves/world_checkpoint.bin");
 * world.snapshots.load_from_file("saves/world_checkpoint.bin");
 * size_t required_bytes = world.snapshots.size_bytes();
 * SnapshotHeader header = world.snapshots.header();
 * ```
 */
class SnapshotFacade {
public:
  explicit constexpr SnapshotFacade(World *world) noexcept : world_(world) {}

  /// Serializes all archetypes, chunk columns, and active entities into a
  /// destination buffer.
  size_t save(void *destination_buffer, size_t max_bytes);

  /// Restores complete simulation state from a serialized snapshot buffer.
  void load(const void *snapshot_buffer, size_t bytes);

  /// Saves the complete simulation state directly to a file on disk.
  bool save_to_file(std::string_view file_path);

  /// Restores the complete simulation state directly from a file on disk.
  bool load_from_file(std::string_view file_path);

  /// Returns the total buffer size in bytes required to store a snapshot of
  /// current state.
  [[nodiscard]] size_t size_bytes() const noexcept;

  /// Returns the snapshot header containing magic numbers, version, and
  /// allocation counts.
  [[nodiscard]] SnapshotHeader header() const noexcept;

private:
  World *world_{nullptr};
};

} // namespace arche
