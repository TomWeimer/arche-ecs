module;

#include <cstddef>
#include <vector>

export module arche:query_cache;

import :component_mask;

export namespace arche {

class Archetype;

struct QueryCacheKey {
  ComponentMask include_mask;
  ComponentMask exclude_mask;

  constexpr bool operator==(const QueryCacheKey &other) const noexcept {
    return include_mask == other.include_mask &&
        exclude_mask == other.exclude_mask;
  }
};

struct QueryCacheKeyHash {
  [[nodiscard]] size_t operator()(const QueryCacheKey &k) const noexcept {
    ComponentMaskHash h;
    return h(k.include_mask) ^ (h(k.exclude_mask) << 1);
  }
};

struct QueryCache {
  ComponentMask include_mask;
  ComponentMask exclude_mask;
  std::vector<Archetype *> matching_archetypes;
};

} // namespace arche
