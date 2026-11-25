module;

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <memory>
#include <span>
#include <string_view>

export module arche:snapshot;

export import :snapshot.detail.header;
import :archetype;
import :chunk;
import :chunk_pool;
import :slot_map;

export namespace arche::internal {

// =============================================================================
// Snapshot Input / Output POD Boundaries
// =============================================================================

/**
 * @struct SnapshotTarget
 * @brief Plain Old Data (POD) descriptor specifying destination memory for serialization.
 */
struct SnapshotTarget {
  /// Pointer to the writable contiguous destination memory buffer.
  void *buffer{nullptr};

  /// Maximum byte capacity available in the destination buffer.
  size_t max_bytes{0};
};

/**
 * @struct SnapshotSource
 * @brief Plain Old Data (POD) descriptor specifying source memory for deserialization.
 */
struct SnapshotSource {
  /// Pointer to the readable contiguous snapshot data buffer.
  const void *buffer{nullptr};

  /// Exact byte count contained in the source buffer.
  size_t byte_count{0};
};

/**
 * @struct SnapshotStorageView
 * @brief Non-owning view bundling the core ECS memory structures required for persistence.
 *
 * Inverts dependencies so that Layer 3 (Snapshot) operates strictly on Layer 0-2
 * storage primitives without coupling to Layer 5 (World).
 */
struct SnapshotStorageView {
  /// Mutable reference to the global entity slot map table.
  SlotMap &slot_map;

  /// Read-only span over all unique archetypes registered in the graph.
  std::span<const std::unique_ptr<Archetype>> archetypes;

  /// Mutable reference to the 64KB chunk page memory pool.
  ChunkPool &chunk_pool;

  /// Target page size in bytes for each memory chunk (typically 64KB).
  size_t chunk_size{65536};

  /// Simulation version sequence restored from or saved to the snapshot header.
  uint32_t &version;
};

// =============================================================================
// The Snapshot Contract (Compile-Time Invariant Specification)
// =============================================================================

/**
 * @concept SnapshotEngineContract
 * @brief Formal C++20 compile-time concept specifying the behavioral contract of a persistence engine.
 *
 * Enforces that an engine guarantees non-throwing size queries, header extraction,
 * memory-to-memory save/load, and direct file persistence without virtual overhead.
 */
template <typename T>
concept SnapshotEngineContract = requires(
    SnapshotStorageView storage,
    SnapshotTarget target,
    SnapshotSource source,
    std::string_view path
) {
  { T::size_bytes(storage) } noexcept -> std::same_as<size_t>;
  { T::header(storage) } noexcept -> std::same_as<SnapshotHeader>;
  { T::save(storage, target) } -> std::same_as<size_t>;
  { T::load(storage, source) } -> std::same_as<void>;
  { T::save_to_file(storage, path) } -> std::same_as<bool>;
  { T::load_from_file(storage, path) } -> std::same_as<bool>;
};

// =============================================================================
// Snapshot Engine Public Contract
// =============================================================================

/**
 * @class Snapshot
 * @brief Standalone binary persistence engine fulfilling SnapshotEngineContract.
 *
 * Serializes active chunk memory pages, SlotMap generation tables, and
 * archetype schemas directly into contiguous memory buffers or disk checkpoint
 * files with zero per-entity overhead.
 *
 * ```cpp
 * // Example:
 * size_t required = Snapshot::size_bytes(storage_view);
 * std::vector<uint8_t> buffer(required);
 * Snapshot::save(storage_view, SnapshotTarget{buffer.data(), buffer.size()});
 * Snapshot::load(storage_view, SnapshotSource{buffer.data(), buffer.size()});
 * ```
 */
class Snapshot {
public:
  /**
   * @brief Computes the exact buffer size in bytes required to store a snapshot of the storage state.
   *
   * @param storage Non-owning view over slot map, archetypes, chunk pool, and version.
   * @return Required capacity in bytes: sizeof(SnapshotHeader) + slot_bytes + metadata + chunk_bytes.
   */
  [[nodiscard]] static size_t size_bytes(
      const SnapshotStorageView &storage
  ) noexcept;

  /**
   * @brief Constructs the snapshot header containing magic numbers, version, and allocation counts.
   *
   * @param storage Non-owning view over the ECS storage.
   * @return A fully populated SnapshotHeader POD.
   */
  [[nodiscard]] static SnapshotHeader header(
      const SnapshotStorageView &storage
  ) noexcept;

  /**
   * @brief Serializes all archetypes, chunk columns, and active entities into a destination buffer.
   *
   * @param storage The ECS storage state to serialize.
   * @param target POD descriptor specifying the destination buffer and capacity.
   * @return Total bytes written to the buffer.
   * @throws std::runtime_error if target.max_bytes is smaller than the required size.
   */
  static size_t save(const SnapshotStorageView &storage, SnapshotTarget target);

  /**
   * @brief Restores complete simulation state from a serialized snapshot buffer.
   *
   * Reconstructs the SlotMap free list and active generations, recycles orphaned chunks
   * back to the ChunkPool, acquires new chunks as needed, and updates SlotMap entity locations.
   *
   * @param storage The destination ECS storage state to restore into.
   * @param source POD descriptor specifying the source snapshot buffer and length.
   * @throws std::runtime_error on buffer underflow or invalid magic header.
   */
  static void load(const SnapshotStorageView &storage, SnapshotSource source);

  /**
   * @brief Saves the complete simulation state directly to a binary file on disk.
   *
   * @param storage The ECS storage state to serialize.
   * @param file_path Filesystem path for the checkpoint file.
   * @return True if the file was successfully opened and written.
   */
  static bool save_to_file(
      const SnapshotStorageView &storage,
      std::string_view file_path
  );

  /**
   * @brief Restores the complete simulation state directly from a binary file on disk.
   *
   * @param storage The destination ECS storage state to restore into.
   * @param file_path Filesystem path of the checkpoint file.
   * @return True if the file was successfully read and applied.
   */
  static bool load_from_file(
      const SnapshotStorageView &storage,
      std::string_view file_path
  );
};

static_assert(SnapshotEngineContract<Snapshot>);

} // namespace arche
