#pragma once

#include "arche/archetype/archetype_graph.hpp"
#include "arche/chunk/chunk.hpp"
#include "arche/chunk/chunk_pool.hpp"
#include "arche/component/type_id.hpp"
#include "arche/entity/slot_map.hpp"
#include "arche/hardware/hardware.hpp"
#include "arche/query/query_cache.hpp"
#include "arche/resource/resource.hpp"
#include "arche/system/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace arche {

/**
 * @struct WorldStorage
 * @brief Flat, cache-efficient storage container encapsulating all ECS simulation state.
 *
 * @details
 * In accordance with GEMINI.md Hard Invariants:
 * - Plain Old Data (POD) struct with zero virtual methods and zero polymorphic overhead.
 * - Centralizes generational entity slot map, contiguous 64 KB chunk memory pool,
 *   archetype transition graph, and query acceleration caches.
 */
struct WorldStorage {
  uint32_t current_version{1};
  uint32_t chunk_size{65536};

  TypeRegistry registry;
  ResourceContainer resources;
  Scheduler scheduler;
  std::vector<std::string> installed_plugins;

  SlotMap slot_map;
  ChunkPool chunk_pool;
  ArchetypeGraph graph;
  std::vector<std::vector<Chunk *>> archetype_chunks;
  std::unordered_map<QueryCacheKey, std::unique_ptr<QueryCache>, QueryCacheKeyHash> query_caches{};

  explicit WorldStorage(size_t chunk_sz = 65536)
      : chunk_size(static_cast<uint32_t>(Hardware::optimal_chunk_size(chunk_sz))),
        chunk_pool(chunk_size),
        graph(registry, chunk_size) {}

  ~WorldStorage() = default;

  WorldStorage(const WorldStorage &) = delete;
  WorldStorage &operator=(const WorldStorage &) = delete;
  WorldStorage(WorldStorage &&) = delete;
  WorldStorage &operator=(WorldStorage &&) = delete;
};

} // namespace arche
