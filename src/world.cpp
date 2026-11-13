#include "arche/world.hpp"
#include "arche/hardware.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace arche {

// =============================================================================
// World: Constructors & Destructor
// =============================================================================

/**
 * @brief Creates a new World with the given chunk memory size.
 *
 * @param chunk_size Target memory block size for chunks in bytes (defaults to
 * 64 KB). The size is adjusted to match system virtual memory page sizes.
 */
World::World(size_t chunk_size)
    : chunk_size_(
          static_cast<uint32_t>(Hardware::optimal_chunk_size(chunk_size))
      ),
      chunk_pool_(chunk_size_), graph_(registry_, chunk_size_) {}

/**
 * @brief Destroys the World and frees all allocated chunk memory.
 */
World::~World() { clear(); }

/**
 * @brief Destroys all entities and returns all chunk buffers to the pool.
 *
 * @details
 * ### Execution Steps:
 * 1. **Reset SlotMap:** Clears all slot records and resets the generational
 * free list.
 * 2. **Recycle Chunks:** Loops through every instantiated archetype in the
 * graph and returns all allocated chunks back to the ChunkPool.
 * 3. **Clear Chunk Lists:** Empties the archetype chunk tracking vectors.
 * 4. **Release Memory Pool:** Clears the ChunkPool, freeing all allocated
 * chunk buffers back to the operating system.
 */
void World::clear() {
  // 1. Reset all slot map records and generational counters
  slot_map_.clear();

  // 2. Return all allocated archetype chunks back to the memory pool
  for (auto &arch_ptr : graph_.archetypes()) {
    auto &chunks = get_archetype_chunks(arch_ptr.get());
    for (Chunk *c : chunks) {
      chunk_pool_.release_buffer(c);
    }
    chunks.clear();
  }

  // 3. Clear archetype tracking lists and free pool blocks to the system
  archetype_chunks_.clear();
  chunk_pool_.clear();
}

// =============================================================================
// Entity Lifecycle
// =============================================================================

/**
 * @brief Destroys an entity and reclaims its slot in constant time.
 *
 * @details
 * ### Execution Steps:
 * 1. **Check Liveness:** Verifies that the entity is alive in the SlotMap
 * (returns false if already destroyed).
 * 2. **Lookup Location:** Reads the entity's current Archetype, Chunk, and
 * row index from the SlotMap.
 * 3. **Swap-and-Pop:** Moves the last entity in the chunk into the vacated
 * row, destructs any non-trivial components, and updates the relocated
 * entity's row in the SlotMap.
 * 4. **Update Archetype:** Decrements the archetype's active entity count.
 * 5. **Recycle Memory:** If the chunk became completely empty (and is not the
 * archetype's only chunk), returns it to the ChunkPool.
 * 6. **Free Handle:** Recycles the entity handle in the SlotMap by
 * incrementing its generation and adding it to the free list.
 *
 * @param entity The entity handle to destroy.
 * @return true if the entity was found and destroyed; false if already dead.
 */
bool World::destroy_entity(Entity entity) {
  // 1. Retrieve the entity's storage record (validating liveness in a single lookup)
  const EntityRecord *record = slot_map_.find_record(entity);
  if (!record) {
    return false;
  }

  Archetype *arch = record->archetype;
  Chunk *chunk = record->chunk;
  assert(arch != nullptr && chunk != nullptr);

  // 2. Swap the last entity into this row, pop it, and run destructors for its
  // components
  chunk->swap_and_pop(record->row, slot_map_, arch->column_layouts());
  arch->decrement_entity_count();

  // 4. Mark the chunk as modified
  chunk->advance_structure_version(current_tick_);

  // 5. Return chunk to the pool if it became completely empty and isn't the
  // sole chunk
  if (chunk->is_empty() && get_archetype_chunks(arch).size() > 1) {
    recycle_chunk(arch, chunk);
  }

  // 6. Invalidate and reclaim the entity handle in the slot map
  slot_map_.free(entity);
  return true;
}

// =============================================================================
// Internal Storage & Slot Allocation
// =============================================================================

/**
 * @brief Finds or allocates an open slot in a chunk for the given archetype,
 * and writes the entity handle into it.
 *
 * @param arch Archetype where the entity will reside.
 * @param entity Entity handle being placed.
 * @return A pair containing the target Chunk pointer and row index.
 */
std::pair<Chunk *, uint32_t> World::allocate_slot(
    Archetype *arch,
    Entity entity
) {
  // 1. Acquire an active chunk with available capacity (allocating from pool if needed)
  Chunk *active = acquire_chunk_for(arch);

  // 2. Append the entity handle to the active chunk and return its location
  const uint32_t row = active->append_entity(entity);
  return {active, row};
}

/**
 * @brief Recycles an empty chunk back to the memory pool.
 *
 * @param arch Archetype owning the chunk.
 * @param chunk Chunk pointer to recycle.
 */
void World::recycle_chunk(Archetype *arch, Chunk *chunk) {
  auto &chunks = get_archetype_chunks(arch);
  auto it = std::find(chunks.begin(), chunks.end(), chunk);
  if (it == chunks.end()) {
    return;
  }

  // Remove from active archetype list and return raw memory to the recycling
  // pool
  chunks.erase(it);
  chunk_pool_.release_buffer(chunk);
}

// =============================================================================
// Internal Entity Migration & Component Relocation
// =============================================================================

namespace {

/**
 * @brief Relocates components across archetype chunks during entity migration.
 */
void copy_shared_components(
    Chunk *src_chunk,
    uint32_t src_row,
    Archetype *src_arch,
    Chunk *dst_chunk,
    uint32_t dst_row,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) noexcept {
  for (size_t i = 0; i < dst_arch->component_count(); ++i) {
    const auto &dst_layout = dst_arch->column_layout(i);
    const TypeID tid = dst_layout.type_id;
    void *dst_ptr = dst_chunk->component_element(dst_layout, dst_row);

    // Case 1: Newly added component data being initialized into the destination
    // row
    if (tid == mutated_type && added_component_data) {
      std::memcpy(dst_ptr, added_component_data, dst_layout.size);
      continue;
    }

    // Case 2: Shared component present in both source and destination
    // archetypes
    const int16_t src_col_idx = src_arch->column_index(tid);
    if (src_col_idx >= 0) {
      const auto &src_layout = src_arch->column_layout(
          static_cast<size_t>(src_col_idx)
      );
      void *src_ptr = src_chunk->component_element(src_layout, src_row);
      src_layout.descriptor->relocate(dst_ptr, src_ptr);
    }
  }
}

} // namespace

/**
 * @brief Moves an entity from one archetype to another, transferring shared
 * components and updating the slot map.
 *
 * @details
 * ### Execution Steps:
 * 1. Allocates a new row and writes the entity handle in the destination
 * archetype chunk.
 * 2. Copies or moves all existing components that exist in both archetypes.
 * 3. Writes newly added component data if one is being attached.
 * 4. Uses swap-and-pop to remove the entity from the old chunk.
 * 5. Updates the SlotMap so future lookups point to the new location.
 *
 * @param entity Entity handle being migrated.
 * @param old_record Source entity location record.
 * @param src_arch Source archetype.
 * @param dst_arch Destination archetype.
 * @param mutated_type Component TypeID being added or removed.
 * @param added_component_data Pointer to component data being added (nullptr on
 * removal).
 */
void World::migrate_entity(
    Entity entity,
    const EntityRecord &old_record,
    Archetype *src_arch,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) {
  // 1. Allocate a row in the destination archetype chunk
  auto [dst_chunk, dst_row] = allocate_slot(dst_arch, entity);
  Chunk *src_chunk = old_record.chunk;
  assert(src_chunk != nullptr);

  // 2. Transfer shared component data and write newly added component (if any)
  copy_shared_components(
      src_chunk,
      old_record.row,
      src_arch,
      dst_chunk,
      dst_row,
      dst_arch,
      mutated_type,
      added_component_data
  );

  dst_arch->increment_entity_count();

  // 3. Swap-and-pop the vacated row from the source chunk to preserve dense
  // packing
  src_chunk->swap_and_pop(
      old_record.row, slot_map_, src_arch->column_layouts()
  );
  src_arch->decrement_entity_count();

  // 4. Update the slot map record to point to the new archetype, chunk, and row
  slot_map_.update_location(entity, dst_arch, dst_chunk, dst_row);
}

// =============================================================================
// Deferred Command Execution Handlers
// =============================================================================

/**
 * @brief Applies a deferred component addition command during playback.
 *
 * @param entity Entity handle to modify.
 * @param tid TypeID of the component being added.
 * @param data Pointer to the component data to attach.
 */
void World::apply_command_add_component(
    Entity entity,
    TypeID tid,
    const void *data
) {
  // 1. Retrieve record (skipping if entity was destroyed prior to command playback)
  const EntityRecord *rec = slot_map_.find_record(entity);
  if (!rec || !rec->archetype || rec->archetype->has_component(tid)) {
    return;
  }

  // 2. Follow graph transition edge (+tid) and migrate entity
  Archetype *dst = graph_.traverse_add(rec->archetype, tid);
  migrate_entity(entity, *rec, rec->archetype, dst, tid, data);
}

/**
 * @brief Applies a deferred component removal command during playback.
 *
 * @param entity Entity handle to modify.
 * @param tid TypeID of the component being removed.
 */
void World::apply_command_remove_component(Entity entity, TypeID tid) {
  // 1. Retrieve record (skipping if entity was destroyed prior to command playback)
  const EntityRecord *rec = slot_map_.find_record(entity);
  if (!rec || !rec->archetype || !rec->archetype->has_component(tid)) {
    return;
  }

  // 2. Follow graph transition edge (-tid) and migrate entity
  Archetype *dst = graph_.traverse_remove(rec->archetype, tid);
  migrate_entity(entity, *rec, rec->archetype, dst, tid, nullptr);
}

// =============================================================================
// Batched Entity Destruction Implementation
// =============================================================================

/**
 * @brief Destroys a batch of entities in a single cache-efficient pass.
 *
 * @details
 * 1. Groups alive entities and their row indices by Chunk pointer.
 * 2. Performs batched Swap-and-Pop compaction per chunk (sorted descending to
 *    preserve index validity).
 * 3. Reclaims and invalidates entity handles in the SlotMap in bulk.
 *
 * @param entities Span of Entity handles to destroy.
 */
void World::destroy_entities(std::span<const Entity> entities) {
  if (entities.empty()) {
    return;
  }

  // 1. Group alive entities and their row indices by Chunk pointer
  struct ChunkDeletions {
    Archetype *arch{nullptr};
    std::vector<uint32_t> rows;
  };
  std::unordered_map<Chunk *, ChunkDeletions> chunk_map;

  for (Entity e : entities) {
    // Fast-path lookup combining liveness check and record retrieval (defined
    // in slot_map.hpp)
    const EntityRecord *rec = slot_map_.find_record(e);
    if (!rec) {
      continue;
    }
    auto &entry = chunk_map[rec->chunk];
    entry.arch = rec->archetype;
    entry.rows.push_back(rec->row);
  }

  // 2. Perform batched Swap-and-Pop compaction per chunk
  for (auto &[chunk, info] : chunk_map) {
    std::sort(info.rows.begin(), info.rows.end(), std::greater<uint32_t>());

    chunk->batch_swap_and_pop(
        info.rows, slot_map_, info.arch->column_layouts()
    );
    info.arch->decrement_entity_count(info.rows.size());

    // 2.5 Mark the chunk as modified
    chunk->advance_structure_version(current_tick_);

    if (chunk->is_empty() && get_archetype_chunks(info.arch).size() > 1) {
      recycle_chunk(info.arch, chunk);
    }
  }

  // 3. Invalidate and reclaim entity handles in SlotMap in bulk
  slot_map_.free_batch(entities);
}

} // namespace arche
