module;

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

export module arche:archetype_graph.detail.impl;

import :archetype_graph;
import :archetype.detail.impl;

export namespace arche {

// =============================================================================
// ArchetypeGraph Construction & Root Node
// =============================================================================

ArchetypeGraph::ArchetypeGraph(
    const TypeRegistry &registry,
    uint32_t chunk_size
)
    : registry_(registry), chunk_size_(chunk_size) {
  // 1. Create root archetype node with empty component mask and empty types list.
  auto root_node = std::make_unique<Archetype>(
      0, ComponentMask{}, std::vector<TypeID>{}, registry_, chunk_size_
  );
  root_ = root_node.get();

  // 2. Register empty mask mapping and append to owned archetypes collection.
  mask_to_archetype_[ComponentMask{}] = root_;
  archetypes_.push_back(std::move(root_node));
}

// =============================================================================
// Graph Node Introspection Implementations
// =============================================================================

Archetype *ArchetypeGraph::root() const noexcept { return root_; }

Archetype *ArchetypeGraph::get_archetype(
    const ComponentMask &mask
) const noexcept {
  auto it = mask_to_archetype_.find(mask);
  return it != mask_to_archetype_.end() ? it->second : nullptr;
}

std::span<const std::unique_ptr<Archetype>>
ArchetypeGraph::archetypes() const noexcept {
  return archetypes_;
}

size_t ArchetypeGraph::archetype_count() const noexcept {
  return archetypes_.size();
}

// =============================================================================
// Dynamic Archetype Resolution & Creation Implementations
// =============================================================================

Archetype *ArchetypeGraph::get_or_create_archetype(
    const ComponentMask &mask
) {
  // 1. Return immediately if an archetype with this mask is already registered.
  if (auto *existing = get_archetype(mask)) {
    return existing;
  }

  // 2. Decode component bitmask words into sorted list of TypeIDs.
  std::vector<TypeID> sorted_types;
  for (size_t word_idx = 0; word_idx < ComponentMask::WORDS_COUNT; ++word_idx) {
    uint64_t w = mask.word(word_idx);
    while (w != 0) {
      const uint32_t bit = std::countr_zero(w);
      const TypeID tid = static_cast<TypeID>((word_idx * 64) + bit);
      sorted_types.push_back(tid);
      w &= (w - 1);
    }
  }
  std::sort(sorted_types.begin(), sorted_types.end());

  // 3. Allocate and initialize the new Archetype node.
  const uint32_t new_id = static_cast<uint32_t>(archetypes_.size());
  auto node = std::make_unique<Archetype>(
      new_id, mask, std::move(sorted_types), registry_, chunk_size_
  );
  Archetype *ptr = node.get();
  archetypes_.push_back(std::move(node));
  mask_to_archetype_[mask] = ptr;

  // 4. Reactively evaluate matching against all active query caches.
  for (QueryCache *cache : registered_caches_) {
    if (ptr->matches(
            cache->include_mask, cache->exclude_mask, cache->any_mask
        )) {
      cache->matching_archetypes.push_back(ptr);
    }
  }

  return ptr;
}

Archetype *ArchetypeGraph::get_or_create(
    std::span<const TypeID> types
) {
  // 1. Construct component mask from constituent TypeIDs.
  ComponentMask mask{};
  for (TypeID tid : types) {
    mask.set(tid);
  }
  // 2. Delegate to bitmask resolver.
  return get_or_create_archetype(mask);
}

// =============================================================================
// Graph Transition Navigation Implementations
// =============================================================================

Archetype *ArchetypeGraph::traverse_add(Archetype *from, TypeID type) {
  assert(from != nullptr);

  // 1. Check cached directional edge first for fast-path transition.
  if (Archetype *cached = from->get_add_edge(type)) {
    return cached;
  }

  // 2. Construct destination bitmask by setting the component bit.
  ComponentMask target_mask = from->mask();
  target_mask.set(type);

  // 3. Resolve or instantiate destination archetype.
  Archetype *target = get_or_create_archetype(target_mask);

  // 4. Bidirectionally memoize transition edges between nodes.
  from->set_add_edge(type, target);
  target->set_remove_edge(type, from);
  return target;
}

Archetype *ArchetypeGraph::traverse_remove(
    Archetype *from,
    TypeID type
) {
  assert(from != nullptr);

  // 1. Check cached directional edge first for fast-path transition.
  if (Archetype *cached = from->get_remove_edge(type)) {
    return cached;
  }

  // 2. Construct destination bitmask by clearing the component bit.
  ComponentMask target_mask = from->mask();
  target_mask.reset(type);

  // 3. Resolve or instantiate destination archetype.
  Archetype *target = get_or_create_archetype(target_mask);

  // 4. Bidirectionally memoize transition edges between nodes.
  from->set_remove_edge(type, target);
  target->set_add_edge(type, from);
  return target;
}

// =============================================================================
// Query Cache Registration Implementations
// =============================================================================

void ArchetypeGraph::register_query_cache(QueryCache &cache) {
  // 1. Clear destination cache matches.
  cache.matching_archetypes.clear();

  // 2. Backfill cache with all currently existing matching archetypes.
  for (const auto &node : archetypes_) {
    if (node->matches(
            cache.include_mask, cache.exclude_mask, cache.any_mask
        )) {
      cache.matching_archetypes.push_back(node.get());
    }
  }

  // 3. Register cache pointer for reactive updates on future archetype creations.
  registered_caches_.push_back(&cache);
}

void ArchetypeGraph::unregister_query_cache(QueryCache &cache) {
  // 1. Locate and remove cache from notification list.
  auto it = std::find(
      registered_caches_.begin(), registered_caches_.end(), &cache
  );
  if (it != registered_caches_.end()) {
    registered_caches_.erase(it);
  }
}

} // namespace arche
