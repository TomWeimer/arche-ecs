module;

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

module arche;

import :mutation;

namespace arche {

/// Monotonically increasing simulation version sequence, incremented on
/// mutations.
uint32_t World::version() const noexcept { return storage_->current_version; }

/// Previous simulation version sequence before the most recent tick advancement.
uint32_t World::previous_version() const noexcept {
  return storage_->previous_version;
}

uint32_t World::advance_version() noexcept {
  storage_->previous_version = storage_->current_version;
  uint32_t next = storage_->current_version + 1;
  if (next == 0 || next == SINCE_LAST_FRAME) {
    next = 1;
  }
  storage_->current_version = next;
  return next;
}

void World::set_version(uint32_t v) noexcept {
  storage_->current_version = v;
}

SnapshotStorageView World::snapshot_storage_view() const noexcept {
  return SnapshotStorageView{
      .slot_map = storage_->slot_map,
      .archetypes = storage_->graph.archetypes(),
      .chunk_pool = storage_->chunk_pool,
      .chunk_size = storage_->chunk_size,
      .version = storage_->current_version,
  };
}

QueryCache *World::query_cache(
    const ComponentMask &include,
    const ComponentMask &exclude,
    const ComponentMask &any
) {
  QueryCacheKey key{include, exclude, any};
  auto it = storage_->query_caches.find(key);
  if (it != storage_->query_caches.end()) {
    return it->second.get();
  }

  auto cache = std::make_unique<QueryCache>();
  cache->include_mask = include;
  cache->exclude_mask = exclude;
  cache->any_mask = any;

  for (const auto &arch_ptr : storage_->graph.archetypes()) {
    if (arch_ptr->matches(include, exclude, any)) {
      cache->matching_archetypes.push_back(arch_ptr.get());
    }
  }

  QueryCache *raw_cache = cache.get();
  storage_->query_caches.emplace(key, std::move(cache));
  storage_->graph.register_query_cache(*raw_cache);

  return raw_cache;
}

// =============================================================================
// World: Constructors & Destructor
// =============================================================================

World::World(size_t chunk_size, uint32_t thread_count)
    : storage_(new internal::WorldStorage(chunk_size, thread_count)),
      entities(storage_->slot_map),
      snapshots(
          [](void *ctx) -> SnapshotStorageView {
            return static_cast<World *>(ctx)->snapshot_storage_view();
          },
          this
      ),
      resources(storage_->resources),
      archetypes(storage_->graph) {
  storage_->execution_target =
      std::make_unique<internal::SystemExecutionTarget>(system_execution_target());
}

World::~World() {
  clear();
  delete storage_;
}

void World::run() {
  assert(storage_ != nullptr);
  assert(storage_->execution_target != nullptr);
  if (storage_->thread_pool) {
    storage_->scheduler.run(*storage_->execution_target, *storage_->thread_pool);
  } else {
    storage_->scheduler.run(*storage_->execution_target);
  }
}

void World::run_sequential() {
  assert(storage_ != nullptr);
  assert(storage_->execution_target != nullptr);
  storage_->scheduler.run(*storage_->execution_target);
}

void World::compile() {
  assert(storage_ != nullptr);
  assert(storage_->execution_target != nullptr);
  storage_->scheduler.compile(*storage_->execution_target);
}

std::optional<SystemFacade> World::system(std::string_view name) noexcept {
  assert(storage_ != nullptr);
  auto idx = storage_->scheduler.find_system_index(name);
  if (!idx) {
    return std::nullopt;
  }
  return SystemFacade(
      storage_->scheduler,
      *idx,
      storage_->execution_target.get()
  );
}

void World::set_threads(uint32_t count) {
  const uint32_t threads = count > 0 ? count : Hardware::core_count();
  storage_->thread_pool = std::make_unique<ThreadPool>(threads);
  storage_->parallel_cmd = std::make_unique<ParallelCommandBuffer>(threads + 1);
}

uint32_t World::thread_count() const noexcept {
  return storage_->thread_pool ? storage_->thread_pool->worker_count() : 0;
}

void World::clear() {
  storage_->slot_map.clear();

  for (auto &arch_ptr : storage_->graph.archetypes()) {
    arch_ptr->clear_chunks(storage_->chunk_pool);
  }

  storage_->chunk_pool.purge();
}

// =============================================================================
// Entity Lifecycle
// =============================================================================

bool World::despawn(Entity entity) {
  auto ctx = mutation_context();
  return Mutation::destroy_entity(ctx, entity);
}

void World::despawn(std::span<const Entity> entities) {
  auto ctx = mutation_context();
  Mutation::destroy_entities(ctx, entities);
}

} // namespace arche
