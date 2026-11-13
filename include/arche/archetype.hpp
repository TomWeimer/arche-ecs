#pragma once

#include "arche/chunk.hpp"
#include "arche/component_mask.hpp"
#include "arche/entity.hpp"
#include "arche/math_utils.hpp"
#include "arche/type_registry.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace arche {

// =============================================================================
// Forward Declarations
// =============================================================================

class Archetype;
class ArchetypeGraph;

// =============================================================================
// Archetype DAG Node
// =============================================================================

/**
 * @class Archetype
 * @brief Represents a unique component signature and manages its chunk memory
 * geometry.
 *
 * @details
 * ### What is an Archetype?
 * An Archetype is a unique, canonical combination of component types (e.g.
 * `[Position, Velocity]`). All entities possessing the exact same component set
 * are clustered contiguously into shared 64 KB memory chunks managed by this
 * Archetype.
 *
 * ### Key Architectural Guarantees:
 * 1. **Permanent DAG Node:**
 *    Archetypes are permanent, non-relocating nodes in the global
 * `ArchetypeGraph`. Once created, an Archetype's memory address remains stable
 * across the program lifetime.
 * 2. **O(1) Cached Transition Edges:**
 *    Adding or removing a component transitions an entity along pre-cached
 * pointer edges:
 *    - `get_add_edge(TypeID)`: Pointer to target Archetype possessing
 *`this + T`.
 *    - `get_remove_edge(TypeID)`: Pointer to target Archetype possessing
 *`this - T`. Transitions execute in a single pointer-hop without sorting or
 * hash table lookups.
 * 3. **O(1) Direct Column Lookup:**
 *    `type_to_column_[TypeID]` provides instant resolution of which column
 * stores component `T` (-1 if not present), avoiding linear search during
 * queries.
 * 4. **Precomputed Chunk Layout:**
 *    Determines maximum entity capacity per chunk and exact aligned byte
 * offsets for every column during archetype initialization.
 * 5. **64-Byte Cache-Line Alignment (`alignas(64)`):**
 *    Prevents false sharing when multiple worker threads traverse adjacent
 * archetype nodes.
 */
class alignas(64) Archetype {
public:
  // --- Constants -------------------------------------------------------------

  static constexpr size_t MAX_COMPONENTS = ComponentMask::MAX_COMPONENTS;

  /// 16 elements (64B AVX-512/AVX2/NEON)
  static constexpr uint32_t SIMD_VECTOR_WIDTH = 16;

  /// Tail padding elements per column
  static constexpr uint32_t SIMD_TAIL_PADDING = 16;

private:
  // --- Member Data -----------------------------------------------------------

  /// Unique sequential Archetype ID.
  uint32_t archetype_id_{0};

  /// 128-bit component presence bitmask.
  ComponentMask mask_;

  /// Sorted list of component TypeIDs.
  std::vector<TypeID> types_;

  /// Aligned column layout descriptors.
  std::vector<ComponentColumnLayout> columns_;

  /// Fast TypeID -> Column index map.
  std::array<int16_t, MAX_COMPONENTS> type_to_column_;

  /// Chunk buffer size in bytes (64 KB).
  uint32_t chunk_size_{65536};

  /// Precalculated entity capacity per chunk.
  uint32_t chunk_capacity_{0};

  /// Current number of live entities in archetype.
  uint32_t total_entities_{0};

  /// Cached DAG transition edges for component addition.
  std::vector<Archetype *> edges_add_;

  /// Cached DAG transition edges for component removal.
  std::vector<Archetype *> edges_remove_;

public:
  // --- Constructors & Destructors --------------------------------------------

  /**
   * @brief Constructs an Archetype node and precomputes its chunk column
   * layout.
   *
   * @param id Unique monotonic integer identifying this archetype.
   * @param mask 128-bit bitmask signature representing the component set.
   * @param sorted_types Canonical ascending list of component TypeIDs.
   * @param registry Central type registry providing component descriptors.
   * @param chunk_size Size of memory chunks in bytes (defaults to 64 KB).
   */
  Archetype(
      uint32_t id,
      const ComponentMask &mask,
      std::vector<TypeID> sorted_types,
      const TypeRegistry &registry,
      uint32_t chunk_size = 65536
  )
      : archetype_id_(id), mask_(mask), types_(std::move(sorted_types)),
        chunk_size_(chunk_size) {
    // Initialize column index map to -1 (absent)
    type_to_column_.fill(-1);

    compute_layout(registry);
  }

  ~Archetype() = default;

  // Non-copyable, non-movable: must maintain stable memory addresses for graph
  // edges
  Archetype(const Archetype &) = delete;
  Archetype &operator=(const Archetype &) = delete;
  Archetype(Archetype &&) = delete;
  Archetype &operator=(Archetype &&) = delete;

  // --- Query Bitmask Matching Helper -----------------------------------------

  /**
   * @brief Evaluates whether this archetype satisfies query inclusion/exclusion
   * criteria.
   * @param include Mask of components that must all be present.
   * @param exclude Mask of components that must not be present.
   * @return True if archetype matches the query filter.
   */
  [[nodiscard]] inline bool matches(
      const ComponentMask &include,
      const ComponentMask &exclude
  ) const noexcept {
    return mask_.contains_all(include) &&
        (exclude.empty() || !mask_.contains_any(exclude));
  }

  // --- Identity & Signature Queries ------------------------------------------

  /**
   * @brief Returns the unique numeric ID of this archetype.
   */
  [[nodiscard]] uint32_t id() const noexcept { return archetype_id_; }

  /**
   * @brief Returns the 128-bit component signature bitmask.
   */
  [[nodiscard]] const ComponentMask &mask() const noexcept { return mask_; }

  /**
   * @brief Returns the canonically sorted list of component TypeIDs.
   */
  [[nodiscard]] std::span<const TypeID> types() const noexcept {
    return types_;
  }

  /**
   * @brief Returns the number of distinct component types in this archetype.
   */
  [[nodiscard]] size_t component_count() const noexcept {
    return types_.size();
  }

  /**
   * @brief Returns the maximum number of entities that fit in a single chunk.
   */
  [[nodiscard]] uint32_t chunk_capacity() const noexcept {
    return chunk_capacity_;
  }

  /**
   * @brief Returns the total count of active entities currently in this
   * archetype.
   */
  [[nodiscard]] uint32_t entity_count() const noexcept {
    return total_entities_;
  }

  /**
   * @brief Returns the chunk size in bytes (typically 65,536 bytes / 64 KB).
   */
  [[nodiscard]] uint32_t chunk_size() const noexcept { return chunk_size_; }

  /**
   * @brief Tests whether this archetype contains the given component type in
   * O(1).
   * @param type Component TypeID to check.
   * @return true if present, false otherwise.
   */
  [[nodiscard]] bool has_component(TypeID type) const noexcept {
    return mask_.test(type);
  }

  // --- Column Layout & Index Lookup ------------------------------------------

  /**
   * @brief Resolves the column index for a given component type in O(1).
   *
   * @param type Component TypeID to look up.
   * @return 0-based column index, or -1 if the component is not in this
   * archetype.
   */
  [[nodiscard]] int16_t column_index(TypeID type) const noexcept {
    if (type >= MAX_COMPONENTS) {
      return -1;
    }
    return type_to_column_[type];
  }

  /**
   * @brief Retrieves precomputed column layout by column index.
   * @param index 0-based column index (< component_count()).
   */
  [[nodiscard]] const ComponentColumnLayout &column_layout(
      size_t index
  ) const noexcept {
    assert(index < columns_.size());
    return columns_[index];
  }

  /**
   * @brief Retrieves precomputed column layout by component TypeID.
   * @param type Component TypeID.
   * @return Pointer to layout descriptor, or nullptr if component is absent.
   */
  [[nodiscard]] const ComponentColumnLayout *column_layout_for(
      TypeID type
  ) const noexcept {
    const int16_t idx = column_index(type);
    if (idx < 0) {
      return nullptr;
    }
    return &columns_[static_cast<size_t>(idx)];
  }

  /**
   * @brief Returns a read-only span of all component column layouts.
   */
  [[nodiscard]] std::span<const ComponentColumnLayout>
  column_layouts() const noexcept {
    return columns_;
  }

  // --- Transition DAG Edges (Add / Remove)
  // ------------------------------------

  /**
   * @brief Gets cached target archetype for an addition mutation (+T).
   * @param type Component TypeID being added.
   * @return Target Archetype pointer, or nullptr if not yet navigated/cached.
   */
  [[nodiscard]] Archetype *get_add_edge(TypeID type) const noexcept {
    if (type >= edges_add_.size()) {
      return nullptr;
    }
    return edges_add_[type];
  }

  /**
   * @brief Gets cached target archetype for a removal mutation (-T).
   * @param type Component TypeID being removed.
   * @return Target Archetype pointer, or nullptr if not yet navigated/cached.
   */
  [[nodiscard]] Archetype *get_remove_edge(TypeID type) const noexcept {
    if (type >= edges_remove_.size()) {
      return nullptr;
    }
    return edges_remove_[type];
  }

  /**
   * @brief Caches target archetype for an addition edge (+T).
   * @param type Component TypeID added.
   * @param target Pointer to target Archetype node.
   */
  void set_add_edge(TypeID type, Archetype *target) {
    if (type >= edges_add_.size()) {
      edges_add_.resize(
          std::max<size_t>(type + 1, edges_add_.size() * 2), nullptr
      );
    }
    edges_add_[type] = target;
  }

  /**
   * @brief Caches target archetype for a removal edge (-T).
   * @param type Component TypeID removed.
   * @param target Pointer to target Archetype node.
   */
  void set_remove_edge(TypeID type, Archetype *target) {
    if (type >= edges_remove_.size()) {
      edges_remove_.resize(
          std::max<size_t>(type + 1, edges_remove_.size() * 2), nullptr
      );
    }
    edges_remove_[type] = target;
  }

  // --- Entity Accounting -----------------------------------------------------

  /**
   * @brief Increments the total entity count in this archetype.
   */
  void increment_entity_count(size_t n = 1) noexcept { total_entities_ += n; }

  /**
   * @brief Decrements the total entity count in this archetype.
   */
  void decrement_entity_count(size_t n = 1) noexcept {
    assert(total_entities_ > 0);
    total_entities_ -= n;
  }

  /**
   * @brief Sets the total entity count in this archetype directly (used by
   * snapshot restore).
   * @param count Restored total entity count.
   */
  void set_entity_count(size_t count) noexcept {
    total_entities_ = static_cast<uint32_t>(count);
  }

private:
  // --- Memory Layout Helpers -------------------------------------------------

  /**
   * @brief Rounds an offset up to the nearest multiple of alignment.
   */
  [[nodiscard]] static constexpr uint32_t _align_up(
      uint32_t offset,
      uint32_t alignment
  ) noexcept {
    if (alignment <= 1) {
      return offset;
    }
    return arche::math::align_up(offset, alignment);
  }

  /**
   * @brief Computes the sum of raw component sizes per entity (including Entity
   * handle).
   */
  [[nodiscard]] inline uint32_t _calculate_entity_size_sum(
      const TypeRegistry &registry
  ) const {
    uint32_t total = static_cast<uint32_t>(sizeof(Entity));
    for (TypeID t : types_) {
      const auto &desc = registry.get_descriptor(t);
      total += desc.size;
    }
    return total;
  }

  /**
   * @brief Calculates how many entities fit in a single 64 KB chunk.
   */
  [[nodiscard]] inline uint32_t _calculate_chunk_capacity(
      uint32_t entity_size_sum
  ) const noexcept {
    constexpr uint32_t
        CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES; // 128 bytes
    constexpr uint32_t MAX_ALIGN_PADDING_ALLOWANCE = 256;

    // Overhead accounts for header, alignment padding, and SIMD tail padding
    const uint32_t tail_padding_bytes = static_cast<uint32_t>(
        (sizeof(Entity) + entity_size_sum) * SIMD_TAIL_PADDING
    );

    const uint32_t total_overhead = CHUNK_HEADER_RESERVE +
        MAX_ALIGN_PADDING_ALLOWANCE + tail_padding_bytes;

    const uint32_t available_bytes = (chunk_size_ > total_overhead)
        ? (chunk_size_ - total_overhead)
        : 1;

    const uint32_t raw_capacity = available_bytes / entity_size_sum;

    // Round down to nearest multiple of SIMD_VECTOR_WIDTH using
    // arche::math::round_down_simd
    const uint32_t aligned_capacity = (raw_capacity >= SIMD_VECTOR_WIDTH)
        ? arche::math::round_down_simd<SIMD_VECTOR_WIDTH>(raw_capacity)
        : raw_capacity;

    return (aligned_capacity == 0) ? 1 : aligned_capacity;
  }

  /**
   * @brief Positions Entity handle array and component columns into chunk
   * memory.
   */
  inline void _build_column_layouts(const TypeRegistry &registry) {
    constexpr uint32_t
        CHUNK_HEADER_RESERVE = ChunkGeometry::HEADER_BYTES; // 128 bytes

    // Offset 0 of chunk payload holds the dense Entity ID array:
    // Entity[capacity]
    uint32_t current_offset = CHUNK_HEADER_RESERVE;
    current_offset = _align_up(current_offset, alignof(Entity));
    // Allocate Entity array with SIMD tail padding
    current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) *
        static_cast<uint32_t>(sizeof(Entity));

    // Layout each component column sequentially with hardware alignment
    for (size_t i = 0; i < types_.size(); ++i) {
      TypeID t = types_[i];
      const auto &desc = registry.get_descriptor(t);

      current_offset = _align_up(current_offset, desc.alignment);

      ComponentColumnLayout layout;
      layout.type_id = t;
      layout.size = desc.size;
      layout.alignment = desc.alignment;
      layout.chunk_byte_offset = current_offset;
      layout.descriptor = &desc;

      columns_.push_back(layout);

      // Maps this component's TypeID to its column index for instant O(1)
      // lookup without linear search during query iteration
      type_to_column_[t] = static_cast<int16_t>(i);

      // Allocate column storage plus 16 elements of tail padding
      current_offset += (chunk_capacity_ + SIMD_TAIL_PADDING) * desc.size;
    }

    // Guarantee that calculated SoA slices fit within the allocated chunk size
    assert(
        current_offset <= chunk_size_ &&
        "Chunk layout calculation exceeded chunk boundaries!"
    );
  }

  // --- Internal Memory Layout Calculation ------------------------------------

  /**
   * @brief Pre-calculates aligned byte offsets and chunk entity capacity.
   *
   * Geometry Layout within each 64 KB Chunk:
   * ```text
   * [ ChunkHeader (64B) ]
   * [ Entity IDs Array: Entity[capacity] (aligned to alignof(Entity)) ]
   * [ Column 0: Type0[capacity] (aligned to alignof(Type0)) ]
   * [ Column 1: Type1[capacity] (aligned to alignof(Type1)) ]
   * ...
   * ```
   */
  void compute_layout(const TypeRegistry &registry) {
    columns_.clear();
    columns_.reserve(types_.size());

    if (types_.empty()) {
      // Empty archetype (root): dummy capacity
      chunk_capacity_ = 65536;
      return;
    }

    const uint32_t entity_size_sum = _calculate_entity_size_sum(registry);
    chunk_capacity_ = _calculate_chunk_capacity(entity_size_sum);
    _build_column_layouts(registry);
  }
};

} // namespace arche
