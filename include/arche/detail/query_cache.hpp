#pragma once

#include "arche/component_mask.hpp"

#include <vector>

namespace arche {

// Forward declarations
class Archetype;

// =============================================================================
// Flat Reactive Query Cache
// =============================================================================

/**
 * @struct QueryCacheKey
 * @brief Hash key for query cache deduplication by component masks.
 */
struct QueryCacheKey {
  ComponentMask include_mask;
  ComponentMask exclude_mask;

  constexpr bool operator==(const QueryCacheKey &other) const noexcept {
    return include_mask == other.include_mask &&
        exclude_mask == other.exclude_mask;
  }
};

/**
 * @struct QueryCacheKeyHash
 * @brief Hash functor for QueryCacheKey.
 */
struct QueryCacheKeyHash {
  [[nodiscard]] size_t operator()(const QueryCacheKey &k) const noexcept {
    ComponentMaskHash h;
    return h(k.include_mask) ^ (h(k.exclude_mask) << 1);
  }
};

/**
 * @struct QueryCache
 * @brief Pure flat-data cache of matching archetype pointers for a specific
 * query signature.
 *
 * @details
 * - Zero virtual functions, zero abstract classes, zero polymorphic interfaces.
 * - Owned centrally by World.
 * - When a new archetype is added to the graph, it is tested against active
 * query caches once.
 * - Queries iterate over matching_archetypes directly with zero bitmask
 * evaluations.
 */
struct QueryCache {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  std::vector<Archetype *> matching_archetypes;
};

} // namespace arche