module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

export module arche:sort;

export import :sort.detail.permute;
import :archetype;
import :chunk;
import :slot_map;
import :component_id;

export namespace arche::internal {

// =============================================================================
// Sort POD Input Boundary
// =============================================================================

/**
 * @struct SortTarget
 * @brief Plain Old Data (POD) descriptor defining the target archetype, memory pages,
 * and slot map routing state for an in-place permutation or sorting operation.
 *
 * Encapsulates the execution boundary for archetype-level chunk column sorting.
 */
struct SortTarget {
  /// Pointer to the target archetype whose entities are being sorted.
  Archetype *arch{nullptr};

  /// Read-only span of contiguous chunk pointers holding the archetype's data columns.
  std::span<Chunk *const> chunks{};

  /// Mutable reference to the global entity slot map for updating chunk coordinate records.
  SlotMap &slot_map;

  /// Simulation version sequence used to stamp chunk headers following permutation.
  uint32_t version{0};
};

// =============================================================================
// Low-Level Detail Prototypes
// =============================================================================

namespace detail {

/**
 * @brief Performs an O(N) Least-Significant-Digit (LSD) Radix Sort on an array of key-index pairs.
 *
 * @tparam KeyIndex Element type with an integral `.key` field.
 * @param elements Mutable vector of key-index pairs sorted in-place.
 */
template <typename KeyIndex>
void radix_sort_key_indices(std::vector<KeyIndex> &elements);

} // namespace detail

// =============================================================================
// Free Function Prototypes
// =============================================================================

/**
 * @brief Sorts all entities within an archetype in-place using a custom component comparator.
 *
 * Traverses all chunks in the archetype, extracts component pointers, computes a stable
 * permutation order based on `comp`, and reorders all component columns and entity IDs
 * simultaneously while synchronizing SlotMap routing records.
 *
 * @tparam Component The component type to sort by.
 * @tparam Compare Binary predicate function: bool(const Component&, const Component&).
 * @param arch The archetype to sort.
 * @param chunks The memory chunks of the archetype.
 * @param slot_map The global entity routing table to update with new chunk coordinates.
 * @param version The new structure version sequence to stamp on chunks.
 * @param comp The comparison predicate.
 */
template <typename Component, typename Compare>
void sort_archetype(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    Compare &&comp
);

/**
 * @brief Sorts all entities within an archetype in-place using a component key extractor.
 *
 * For 1-8 byte integral keys, automatically applies an O(N) Radix sort. For other key types,
 * falls back to std::stable_sort. Synchronizes all columns and SlotMap records.
 *
 * @tparam Component The component type to sort by.
 * @tparam KeyExtractor Unary function: KeyType(const Component&).
 * @param arch The archetype to sort.
 * @param chunks The memory chunks of the archetype.
 * @param slot_map The global entity routing table to update.
 * @param version The new structure version sequence.
 * @param key_fn Key extraction function.
 */
template <typename Component, typename KeyExtractor>
void sort_archetype_by_key(
    Archetype *arch,
    std::span<Chunk *const> chunks,
    SlotMap &slot_map,
    uint32_t version,
    KeyExtractor &&key_fn
);

// =============================================================================
// Sort Subsystem Contract
// =============================================================================

/**
 * @class Sort
 * @brief Public sorting engine contract. Provides static sorting, key extraction,
 * and permutation operations over archetype chunk columns.
 *
 * Implements cache-friendly spatial reordering (Morton order / Hilbert curve sorting)
 * to maximize CPU hardware cache locality during sequential and parallel query iteration.
 */
class Sort {
public:
  /**
   * @brief Sorts an archetype's entities by a component comparator.
   *
   * @tparam Component The component type to inspect.
   * @tparam Compare Binary predicate: bool(const Component&, const Component&).
   * @param target POD descriptor bundling archetype, chunks, slot map, and version.
   * @param comp Comparison predicate.
   */
  template <typename Component, typename Compare>
  static void by_component(const SortTarget &target, Compare &&comp);

  /**
   * @brief Sorts an archetype's entities by a key extractor function.
   *
   * @tparam Component The component type to inspect.
   * @tparam KeyExtractor Unary function returning a sortable key.
   * @param target POD descriptor bundling archetype, chunks, slot map, and version.
   * @param key_fn Key extraction callable.
   */
  template <typename Component, typename KeyExtractor>
  static void by_key(const SortTarget &target, KeyExtractor &&key_fn);

  /**
   * @brief Permutes an archetype's component columns and entity IDs according to a target index map.
   *
   * Cycles through disjoint permutation orbits in-place with minimal scratch memory,
   * updates the SlotMap for every affected entity, and stamps the structural version.
   *
   * @param target POD descriptor bundling archetype, chunks, slot map, and version.
   * @param target_indices Precomputed permutation array where element `i` indicates the destination.
   */
  static void permute(
      const SortTarget &target,
      std::span<const uint32_t> target_indices
  );
};

} // namespace arche
