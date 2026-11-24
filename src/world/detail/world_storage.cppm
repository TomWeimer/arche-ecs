module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

export module arche:world.detail.world_storage;

import :hardware;
import :type_id;
import :chunk;
import :chunk_pool;
import :slot_map;
import :resource;
import :resource.detail.impl;
import :query_cache;
import :archetype_graph;
import :scheduler;
import :plugin;
import :thread_pool;
import :command;

export namespace arche::internal {

/**
 * @struct WorldStorage
 * @brief Flat, cache-efficient storage container encapsulating all ECS
 * simulation state.
 *
 * @details
 * - Plain Old Data (POD) struct with zero virtual methods and zero polymorphic
 * overhead.
 * - Centralizes generational entity slot map, contiguous 64 KB chunk memory
 * pool, archetype transition graph, and query acceleration caches.
 */
struct WorldStorage {
  uint32_t current_version{1};
  uint32_t previous_version{0};
  uint32_t chunk_size{65536};

  TypeRegistry registry;
  ResourceContainer resources;
  Scheduler scheduler;
  PluginRegistry installed_plugins;

  SlotMap slot_map;
  ChunkPool chunk_pool;
  ArchetypeGraph graph;
  std::unordered_map<
      QueryCacheKey,
      std::unique_ptr<QueryCache>,
      QueryCacheKeyHash>
      query_caches{};
  std::unique_ptr<ThreadPool> thread_pool;
  std::unique_ptr<ParallelCommandBuffer> parallel_cmd;

  explicit WorldStorage(
      size_t chunk_sz = 65536,
      uint32_t thread_count = Hardware::core_count()
  )
      : chunk_size(
            static_cast<uint32_t>(Hardware::optimal_chunk_size(chunk_sz))
        ),
        chunk_pool(chunk_size), graph(registry, chunk_size),
        thread_pool(
            std::make_unique<ThreadPool>(
                thread_count > 0 ? thread_count : Hardware::core_count()
            )
        ),
        parallel_cmd(
            std::make_unique<ParallelCommandBuffer>(
                (thread_count > 0 ? thread_count : Hardware::core_count()) + 1
            )
        ) {}

  ~WorldStorage() = default;

  WorldStorage(const WorldStorage &) = delete;
  WorldStorage &operator=(const WorldStorage &) = delete;
  WorldStorage(WorldStorage &&) = delete;
  WorldStorage &operator=(WorldStorage &&) = delete;
};

} // namespace arche::internal
