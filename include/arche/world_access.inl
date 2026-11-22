#pragma once

#include "arche/world.hpp"
#include "arche/world_access.hpp"

namespace arche::internal {

inline WorldStorage &WorldAccess::storage(World &w) noexcept { return w.storage_; }
inline const WorldStorage &WorldAccess::storage(const World &w) noexcept { return w.storage_; }

inline SlotMap &WorldAccess::slot_map(World &w) noexcept { return w.storage_.slot_map; }
inline const SlotMap &WorldAccess::slot_map(const World &w) noexcept { return w.storage_.slot_map; }

inline ChunkPool &WorldAccess::chunk_pool(World &w) noexcept { return w.storage_.chunk_pool; }

inline ArchetypeGraph &WorldAccess::graph(World &w) noexcept { return w.storage_.graph; }
inline const ArchetypeGraph &WorldAccess::graph(const World &w) noexcept { return w.storage_.graph; }

inline std::vector<std::vector<Chunk *>> &WorldAccess::chunks(World &w) noexcept { return w.storage_.archetype_chunks; }
inline const std::vector<std::vector<Chunk *>> &WorldAccess::chunks(const World &w) noexcept { return w.storage_.archetype_chunks; }

inline TypeRegistry &WorldAccess::registry(World &w) noexcept { return w.storage_.registry; }
inline const TypeRegistry &WorldAccess::registry(const World &w) noexcept { return w.storage_.registry; }

inline ResourceContainer &WorldAccess::resources(World &w) noexcept { return w.storage_.resources; }
inline const ResourceContainer &WorldAccess::resources(const World &w) noexcept { return w.storage_.resources; }

inline Scheduler &WorldAccess::scheduler(World &w) noexcept { return w.storage_.scheduler; }
inline const Scheduler &WorldAccess::scheduler(const World &w) noexcept { return w.storage_.scheduler; }

inline std::vector<std::string> &WorldAccess::installed_plugins(World &w) noexcept { return w.storage_.installed_plugins; }

inline uint32_t &WorldAccess::version(World &w) noexcept { return w.storage_.current_version; }
inline uint32_t WorldAccess::version(const World &w) noexcept { return w.storage_.current_version; }
inline uint32_t WorldAccess::advance_version(World &w) noexcept {
  w.storage_.current_version = advance_nonzero(w.storage_.current_version);
  return w.storage_.current_version;
}

inline uint32_t &WorldAccess::chunk_size(World &w) noexcept { return w.storage_.chunk_size; }
inline uint32_t WorldAccess::chunk_size(const World &w) noexcept { return w.storage_.chunk_size; }

inline QueryCache *WorldAccess::query_cache(
    World &w, const ComponentMask &include, const ComponentMask &exclude
) {
  QueryCacheKey key{include, exclude};
  auto it = w.storage_.query_caches.find(key);
  if (it != w.storage_.query_caches.end()) {
    return it->second.get();
  }

  auto cache = std::make_unique<QueryCache>();
  cache->include_mask = include;
  cache->exclude_mask = exclude;

  for (const auto &arch_ptr : w.storage_.graph.archetypes()) {
    const ComponentMask &mask = arch_ptr->mask();
    if (mask.contains_all(include)) {
      if (exclude.empty() || !mask.contains_any(exclude)) {
        cache->matching_archetypes.push_back(arch_ptr.get());
      }
    }
  }

  QueryCache *raw_cache = cache.get();
  w.storage_.query_caches.emplace(key, std::move(cache));
  w.storage_.graph.register_query_cache(raw_cache);

  return raw_cache;
}

} // namespace arche::internal
