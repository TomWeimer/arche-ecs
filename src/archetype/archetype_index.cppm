module;

#include <cstddef>
#include <vector>

export module arche:archetype_index;

import :component_mask;

export namespace arche::internal {

class Archetype;

/**
 * @struct ArchetypeIndexKey
 * @brief Composite 3-mask lookup key uniquely identifying a cached archetype index.
 *
 * Encapsulates compile-time component filtering criteria:
 * - `include_mask`: Required components (conjunction: must have ALL).
 * - `exclude_mask`: Prohibited components (rejection: must have NONE).
 * - `any_mask`: Disjunctive components (disjunction: must have AT LEAST ONE, or empty).
 */
struct ArchetypeIndexKey {
  /// Mask of components that matching archetypes must all possess.
  ComponentMask include_mask;

  /// Mask of components that matching archetypes must not possess.
  ComponentMask exclude_mask;

  /// Mask of components where matching archetypes must possess at least one (if non-empty).
  ComponentMask any_mask;

  /// Strict equality comparison across all three component masks.
  constexpr bool operator==(const ArchetypeIndexKey &other) const noexcept {
    return include_mask == other.include_mask &&
        exclude_mask == other.exclude_mask &&
        any_mask == other.any_mask;
  }
};

/**
 * @struct ArchetypeIndexKeyHash
 * @brief 64-bit hash functor for indexing `ArchetypeIndexKey` in `std::unordered_map`.
 */
struct ArchetypeIndexKeyHash {
  [[nodiscard]] size_t operator()(const ArchetypeIndexKey &k) const noexcept {
    ComponentMaskHash h;
    return h(k.include_mask) ^ (h(k.exclude_mask) << 1) ^ (h(k.any_mask) << 2);
  }
};

/**
 * @struct ArchetypeIndex
 * @brief Reactive index storing pre-matched archetype table pointers for fast query resolution.
 *
 * Maintained persistently by `WorldStorage` and registered with `ArchetypeGraph`.
 * Whenever a new archetype is registered in the graph, it is evaluated against
 * these masks and reactively appended to `matching_archetypes` in $O(1)$ amortized time.
 */
struct ArchetypeIndex {
  /// Required components mask.
  ComponentMask include_mask;

  /// Prohibited components mask.
  ComponentMask exclude_mask;

  /// Disjunctive (OR) components mask.
  ComponentMask any_mask;

  /// Zero-copy list of all live archetype tables matching this cache's bitmasks.
  std::vector<Archetype *> matching_archetypes;
};

// Aliases for compatibility
using QueryCacheKey = ArchetypeIndexKey;
using QueryCacheKeyHash = ArchetypeIndexKeyHash;
using QueryCache = ArchetypeIndex;

} // namespace arche::internal
