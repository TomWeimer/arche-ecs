module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

export module arche:archetype_graph;

import :archetype;
import :component_mask;
import :type_id;
import :query_cache;

export namespace arche {

class ArchetypeGraph {
private:
  const TypeRegistry &registry_;
  uint32_t chunk_size_{65536};
  Archetype *root_{nullptr};
  std::vector<std::unique_ptr<Archetype>> archetypes_;
  std::unordered_map<ComponentMask, Archetype *, ComponentMaskHash>
      mask_to_archetype_;
  std::vector<QueryCache *> registered_caches_{};

public:
  explicit ArchetypeGraph(
      const TypeRegistry &registry,
      uint32_t chunk_size = 65536
  )
      : registry_(registry), chunk_size_(chunk_size) {
    auto root_node = std::make_unique<Archetype>(
        0, ComponentMask{}, std::vector<TypeID>{}, registry_, chunk_size_
    );
    root_ = root_node.get();
    mask_to_archetype_[ComponentMask{}] = root_;
    archetypes_.push_back(std::move(root_node));
  }

  ~ArchetypeGraph() = default;

  ArchetypeGraph(const ArchetypeGraph &) = delete;
  ArchetypeGraph &operator=(const ArchetypeGraph &) = delete;
  ArchetypeGraph(ArchetypeGraph &&) noexcept = default;
  ArchetypeGraph &operator=(ArchetypeGraph &&) noexcept = delete;

  [[nodiscard]] inline Archetype *root() const noexcept { return root_; }

  [[nodiscard]] inline Archetype *get_archetype(
      const ComponentMask &mask
  ) const noexcept {
    auto it = mask_to_archetype_.find(mask);
    return it != mask_to_archetype_.end() ? it->second : nullptr;
  }

  [[nodiscard]] inline std::span<const std::unique_ptr<Archetype>>
  archetypes() const noexcept {
    return archetypes_;
  }

  [[nodiscard]] inline size_t archetype_count() const noexcept {
    return archetypes_.size();
  }

  Archetype *get_or_create_archetype(const ComponentMask &mask) {
    if (auto *existing = get_archetype(mask)) {
      return existing;
    }

    std::vector<TypeID> sorted_types;
    for (size_t word_idx = 0; word_idx < ComponentMask::WORDS_COUNT;
         ++word_idx) {
      uint64_t w = mask.word(word_idx);
      while (w != 0) {
        const uint32_t bit = std::countr_zero(w);
        const TypeID tid = static_cast<TypeID>((word_idx * 64) + bit);
        sorted_types.push_back(tid);
        w &= (w - 1);
      }
    }
    std::sort(sorted_types.begin(), sorted_types.end());

    const uint32_t new_id = static_cast<uint32_t>(archetypes_.size());
    auto node = std::make_unique<Archetype>(
        new_id, mask, std::move(sorted_types), registry_, chunk_size_
    );
    Archetype *ptr = node.get();
    archetypes_.push_back(std::move(node));
    mask_to_archetype_[mask] = ptr;

    for (QueryCache *cache : registered_caches_) {
      if (ptr->matches(cache->include_mask, cache->exclude_mask)) {
        cache->matching_archetypes.push_back(ptr);
      }
    }

    return ptr;
  }

  Archetype *get_or_create(std::span<const TypeID> types) {
    ComponentMask mask{};
    for (TypeID tid : types) {
      mask.set(tid);
    }
    return get_or_create_archetype(mask);
  }

  Archetype *navigate_add(Archetype *from, TypeID type) {
    assert(from != nullptr);
    if (Archetype *cached = from->get_add_edge(type)) {
      return cached;
    }

    ComponentMask target_mask = from->mask();
    target_mask.set(type);

    Archetype *target = get_or_create_archetype(target_mask);
    from->set_add_edge(type, target);
    target->set_remove_edge(type, from);
    return target;
  }

  inline Archetype *traverse_add(Archetype *from, TypeID type) {
    return navigate_add(from, type);
  }

  Archetype *navigate_remove(Archetype *from, TypeID type) {
    assert(from != nullptr);
    if (Archetype *cached = from->get_remove_edge(type)) {
      return cached;
    }

    ComponentMask target_mask = from->mask();
    target_mask.reset(type);

    Archetype *target = get_or_create_archetype(target_mask);
    from->set_remove_edge(type, target);
    target->set_add_edge(type, from);
    return target;
  }

  inline Archetype *traverse_remove(Archetype *from, TypeID type) {
    return navigate_remove(from, type);
  }

  void register_query_cache(QueryCache &cache) {
    cache.matching_archetypes.clear();
    for (const auto &node : archetypes_) {
      if (node->matches(cache.include_mask, cache.exclude_mask)) {
        cache.matching_archetypes.push_back(node.get());
      }
    }
    registered_caches_.push_back(&cache);
  }

  void unregister_query_cache(QueryCache &cache) {
    auto it = std::find(
        registered_caches_.begin(), registered_caches_.end(), &cache
    );
    if (it != registered_caches_.end()) {
      registered_caches_.erase(it);
    }
  }
};

} // namespace arche
