module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module arche:archetype;

export import :entity;
export import :component_mask;
export import :component_id;
export import :hardware;
export import :chunk;
export import :chunk_pool;
export import :archetype_index;

export namespace arche::internal {

class ArchetypeGraph;

// =============================================================================
// Archetype: Unique Component Type Combination Table
// =============================================================================

/**
 * @class Archetype
 * @brief Represents a unique structural permutation of component types and owns its chunk storage.
 *
 * An Archetype groups entities sharing the exact same component set into contiguous 64KB
 * chunk pages. It establishes the physical Structure-of-Arrays (SoA) column layouts, byte
 * offsets, and alignment paddings for all component columns within each chunk.
 *
 * ### Key Responsibilities:
 * - **Column Layout Computation:** Calculates SIMD-aligned column offsets with tail padding.
 * - **Chunk Page Allocation:** Maintains an array of 64KB `Chunk` pages, creating and recycling
 *   them through a `ChunkPool`.
 * - **Transition Graph Edges:** Caches fast-path directional transition pointers for adding
 *   and removing components (`edges_add_`, `edges_remove_`).
 * - **Introspection:** Provides constant-time component layout and offset lookups via
 *   dense indexing (`column_index`, `column_layout`).
 */
class alignas(64) Archetype {
public:
  /// Maximum number of distinct component types supported by the component bitmask.
  static constexpr size_t MAX_COMPONENTS = ComponentMask::MAX_COMPONENTS;

  /// Default SIMD vectorization lane width in elements (e.g. 16 floats for AVX-512 / NEON).
  static constexpr uint32_t SIMD_VECTOR_WIDTH = 16;

  /// Tail padding allowance in elements to ensure safe over-read during SIMD vector loops.
  static constexpr uint32_t SIMD_TAIL_PADDING = 16;

  /**
   * @struct ChunkCoord
   * @brief Decomposed two-dimensional coordinate identifying a row within a specific chunk page.
   */
  struct ChunkCoord {
    /// Zero-based index of the chunk page in this archetype's chunk array.
    uint32_t chunk_index{0};

    /// Zero-based row slot within the identified chunk page.
    uint32_t local_row{0};
  };

private:
  friend class ArchetypeGraph;

  uint32_t archetype_id_{0};
  ComponentMask mask_;
  std::vector<ComponentID> types_;
  std::vector<ComponentColumnLayout> columns_;
  std::array<int16_t, MAX_COMPONENTS> type_to_column_;
  uint32_t chunk_size_{65536};
  uint32_t chunk_capacity_{0};
  uint32_t total_entities_{0};
  std::vector<Chunk *> chunks_{};
  std::vector<Archetype *> edges_add_;
  std::vector<Archetype *> edges_remove_;

public:
  // --- Lifecycle & Construction ----------------------------------------------

  /**
   * @brief Constructs an Archetype with sorted component types and computes chunk layouts.
   * @param id Unique numeric identifier assigned by ArchetypeGraph.
   * @param mask Bitmask representing all constituent component types.
   * @param sorted_types Ascending-ordered array of constituent TypeIDs.
   * @param registry Component metadata registry for size and alignment resolution.
   * @param chunk_size Page memory size in bytes (default: 64 KB).
   */
  Archetype(
      uint32_t id,
      const ComponentMask &mask,
      std::vector<ComponentID> sorted_types,
      const ComponentRegistry &registry,
      uint32_t chunk_size = 65536
  );

  ~Archetype() = default;

  Archetype(const Archetype &) = delete;
  Archetype &operator=(const Archetype &) = delete;
  Archetype(Archetype &&) = delete;
  Archetype &operator=(Archetype &&) = delete;

  // --- Filtering & Matching --------------------------------------------------

  /**
   * @brief Evaluates whether this archetype satisfies query include, exclude, and any masks.
   * @param include Mask containing required components (must be a subset).
   * @param exclude Mask containing forbidden components (must not intersect).
   * @param any Mask containing optional components (must intersect if non-empty).
   * @return true if all filter criteria are met; false otherwise.
   */
  [[nodiscard]] bool matches(
      const ComponentMask &include,
      const ComponentMask &exclude,
      const ComponentMask &any = {}
  ) const noexcept;

  // --- Property & Metadata Introspection -------------------------------------

  /// Returns the dense numeric identifier of this archetype.
  [[nodiscard]] uint32_t id() const noexcept;

  /// Returns the component bitmask representing this archetype's component set.
  [[nodiscard]] const ComponentMask &mask() const noexcept;

  /// Returns a span over the sorted constituent TypeIDs.
  [[nodiscard]] std::span<const ComponentID> types() const noexcept;

  /// Returns the total number of distinct component types in this archetype.
  [[nodiscard]] size_t component_count() const noexcept;

  /// Returns the maximum entity row capacity of each 64KB chunk page in this archetype.
  [[nodiscard]] uint32_t chunk_capacity() const noexcept;

  /// Returns the total number of active entities currently stored across all chunks.
  [[nodiscard]] uint32_t entity_count() const noexcept;

  /// Returns the configured chunk page size in bytes (e.g. 65536).
  [[nodiscard]] uint32_t chunk_size() const noexcept;

  /**
   * @brief Checks if this archetype contains a specific component type.
   * @param type Target component ComponentID.
   * @return true if present; false otherwise.
   */
  [[nodiscard]] bool has_component(ComponentID type) const noexcept;

  // --- Column Layout Lookups -------------------------------------------------

  /**
   * @brief Looks up the column index for a given component type.
   * @param type Target component ComponentID.
   * @return Zero-based column index, or -1 if the component is not in this archetype.
   */
  [[nodiscard]] int16_t column_index(ComponentID type) const noexcept;

  /**
   * @brief Retrieves the column layout descriptor at a specified column index.
   * @param index Column index (must be < component_count()).
   * @return ComponentColumnLayout containing byte offset, element size, and alignment.
   */
  [[nodiscard]] const ComponentColumnLayout &column_layout(
      size_t index
  ) const noexcept;

  /**
   * @brief Looks up the column layout descriptor for a component ComponentID.
   * @param type Target component ComponentID.
   * @return Pointer to layout descriptor, or nullptr if not found.
   */
  [[nodiscard]] const ComponentColumnLayout *column_layout_for(
      ComponentID type
  ) const noexcept;

  /// Returns a span over all component column layout descriptors.
  [[nodiscard]] std::span<const ComponentColumnLayout>
  column_layouts() const noexcept;

  // --- Transition Graph Edges ------------------------------------------------

  /**
   * @brief Retrieves the cached destination archetype when adding a component.
   * @param type Component ComponentID to add.
   * @return Destination archetype pointer, or nullptr if edge not yet cached.
   */
  [[nodiscard]] Archetype *get_add_edge(ComponentID type) const noexcept;

  /**
   * @brief Retrieves the cached destination archetype when removing a component.
   * @param type Component ComponentID to remove.
   * @return Destination archetype pointer, or nullptr if edge not yet cached.
   */
  [[nodiscard]] Archetype *get_remove_edge(ComponentID type) const noexcept;

  // --- Chunk Page Management -------------------------------------------------

  /**
   * @brief Decomposes a global linear entity index into chunk index and local row.
   * @param global_index Linear index across all chunks.
   * @return Decomposed ChunkCoord pair.
   */
  [[nodiscard]] ChunkCoord decompose_index(
      uint32_t global_index
  ) const noexcept;

  /// Returns a const span over all allocated chunk page pointers.
  [[nodiscard]] std::span<Chunk *const> chunks() const noexcept;

  /// Returns a mutable reference to the chunk pointer storage vector.
  [[nodiscard]] std::vector<Chunk *> &chunks() noexcept;

  /**
   * @brief Ensures a writable chunk page exists with remaining capacity, allocating if needed.
   * @param pool Chunk slab pool.
   * @return Pointer to an active chunk capable of receiving entity allocations.
   */
  Chunk *ensure_writable_chunk(ChunkPool &pool);

  /**
   * @brief Recycles an empty chunk page back into the pool and removes it from storage.
   * @param pool Chunk slab pool.
   * @param chunk Chunk page to recycle.
   */
  void recycle_chunk(ChunkPool &pool, Chunk *chunk);

  /**
   * @brief Allocates an entity row slot in an available chunk page.
   * @param pool Chunk slab pool.
   * @param entity Entity handle assigned to the slot.
   * @return Pair of allocated Chunk pointer and local row index.
   */
  std::pair<Chunk *, uint32_t> allocate_slot(
      ChunkPool &pool,
      Entity entity
  );

  /**
   * @brief Releases all chunk pages back into the slab pool and clears the chunk vector.
   * @param pool Chunk slab pool.
   */
  void clear_chunks(ChunkPool &pool);

  /// Increments the total entity count tracked in this archetype.
  void increment_entity_count(size_t n = 1) noexcept;

  /// Decrements the total entity count tracked in this archetype.
  void decrement_entity_count(size_t n = 1) noexcept;

  /// Directly overrides the total entity count in this archetype.
  void set_entity_count(size_t count) noexcept;

private:
  void set_add_edge(ComponentID type, Archetype *target);
  void set_remove_edge(ComponentID type, Archetype *target);

  [[nodiscard]] uint32_t _calculate_entity_size_sum(
      const ComponentRegistry &registry
  ) const;

  [[nodiscard]] uint32_t _calculate_chunk_capacity(
      uint32_t entity_size_sum
  ) const noexcept;

  void _build_column_layouts(const ComponentRegistry &registry);

  void compute_layout(const ComponentRegistry &registry);
};

} // namespace arche
