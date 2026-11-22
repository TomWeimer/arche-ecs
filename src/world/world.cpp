#include "arche/world.hpp"
#include "arche/command.hpp"
#include "archetype/component_migration.hpp"
#include "chunk/chunk_distributor.hpp"
#include "world/world_storage.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace arche {

/// Monotonically increasing simulation version sequence, incremented on
/// mutations.
uint32_t World::version() const noexcept { return storage_->current_version; }

// =============================================================================
// World: Constructors & Destructor
// =============================================================================

World::World(size_t chunk_size)
    : storage_(new internal::WorldStorage(chunk_size)) {}

World::~World() {
  clear();
  delete storage_;
}

void World::clear() {
  storage_->slot_map.clear();

  for (auto &arch_ptr : storage_->graph.archetypes()) {
    auto &chunks = ChunkDistributor::get_archetype_chunks(
        *this, arch_ptr.get()
    );
    for (Chunk *c : chunks) {
      storage_->chunk_pool.release_buffer(c);
    }
    chunks.clear();
  }

  storage_->archetype_chunks.clear();
  storage_->chunk_pool.clear();
}

// =============================================================================
// Entity Lifecycle
// =============================================================================

bool World::despawn(Entity entity) {
  const EntityRecord *record = storage_->slot_map.find_record(entity);
  if (!record) {
    return false;
  }

  Archetype *arch = record->archetype;
  Chunk *chunk = record->chunk;
  assert(arch != nullptr && chunk != nullptr);

  chunk->swap_and_pop(record->row, storage_->slot_map, arch->column_layouts());
  arch->decrement_entity_count();

  chunk->set_structure_version(storage_->current_version);

  if (chunk->is_empty() &&
      ChunkDistributor::get_archetype_chunks(*this, arch).size() > 1) {
    ChunkDistributor::recycle_chunk(*this, arch, chunk);
  }

  storage_->slot_map.free(entity);
  return true;
}

void World::despawn(std::span<const Entity> entities) {
  if (entities.empty()) {
    return;
  }

  struct ChunkDeletions {
    Archetype *arch{nullptr};
    std::vector<uint32_t> rows;
  };
  std::unordered_map<Chunk *, ChunkDeletions> chunk_map;

  for (Entity e : entities) {
    const EntityRecord *rec = storage_->slot_map.find_record(e);
    if (!rec) {
      continue;
    }
    auto &entry = chunk_map[rec->chunk];
    entry.arch = rec->archetype;
    entry.rows.push_back(rec->row);
  }

  for (auto &[chunk, info] : chunk_map) {
    std::sort(info.rows.begin(), info.rows.end(), std::greater<uint32_t>());

    chunk->batch_swap_and_pop(
        info.rows, storage_->slot_map, info.arch->column_layouts()
    );
    info.arch->decrement_entity_count(info.rows.size());

    chunk->set_structure_version(storage_->current_version);

    if (chunk->is_empty() &&
        ChunkDistributor::get_archetype_chunks(*this, info.arch).size() > 1) {
      ChunkDistributor::recycle_chunk(*this, info.arch, chunk);
    }
  }

  storage_->slot_map.free_batch(entities);
}

} // namespace arche
