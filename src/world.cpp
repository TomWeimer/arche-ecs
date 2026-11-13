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
  // 1. Retrieve the entity's storage record (validating liveness in a single
  // lookup)
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
  // 1. Acquire an active chunk with available capacity (allocating from pool if
  // needed)
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
  // 1. Retrieve record (skipping if entity was destroyed prior to command
  // playback)
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
  // 1. Retrieve record (skipping if entity was destroyed prior to command
  // playback)
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

// =============================================================================
// In-Place Cycle-Chasing Permutation Helpers
// =============================================================================

namespace {

/**
 * @brief Resolves a pointer to the Entity handle at a global archetype index.
 *
 * @param arch Pointer to the target Archetype.
 * @param chunks Vector of chunks owned by the archetype.
 * @param global_idx Flat global index within the archetype [0, entity_count).
 * @return Pointer to the Entity handle inside its chunk memory slice.
 */
inline Entity *resolve_entity_ptr(
    const Archetype *arch,
    const std::vector<Chunk *> &chunks,
    uint32_t global_idx
) noexcept {
  const Archetype::ChunkCoord coord = arch->decompose_index(global_idx);
  return &chunks[coord.chunk_index]->entity_slice()[coord.local_row];
}

/**
 * @brief Resolves a raw byte pointer to a component element at a global
 * archetype index.
 *
 * @param arch Pointer to the target Archetype.
 * @param chunks Vector of chunks owned by the archetype.
 * @param layout Column layout descriptor defining component element offset and
 * size.
 * @param global_idx Flat global index within the archetype [0, entity_count).
 * @return Raw pointer to the component element within its chunk column.
 */
inline void *resolve_component_ptr(
    const Archetype *arch,
    const std::vector<Chunk *> &chunks,
    const ComponentColumnLayout &layout,
    uint32_t global_idx
) noexcept {
  const Archetype::ChunkCoord coord = arch->decompose_index(global_idx);
  return chunks[coord.chunk_index]->component_element(layout, coord.local_row);
}

/**
 * @brief Updates the SlotMap routing record for an entity at its new global
 * archetype index.
 *
 * @param arch Pointer to the target Archetype.
 * @param chunks Vector of chunks owned by the archetype.
 * @param slot_map Global SlotMap to update.
 * @param entity Entity handle being re-anchored.
 * @param global_idx Flat global index within the archetype where the entity now
 * resides.
 */
inline void sync_entity_slot(
    const Archetype *arch,
    const std::vector<Chunk *> &chunks,
    SlotMap &slot_map,
    Entity entity,
    uint32_t global_idx
) noexcept {
  const Archetype::ChunkCoord coord = arch->decompose_index(global_idx);
  slot_map.update_record(entity, chunks[coord.chunk_index], coord.local_row);
}

/**
 * @brief Permutes a single component column across all chunks in-place using
 * cycle chasing.
 *
 * @details
 * Walks through disjoint permutation cycles in O(N) time. Uses a small scratch
 * buffer `temp` sized to the component's element size to store the cycle head,
 * avoiding full-column allocations.
 *
 * ### Cycle-Chasing Example Walkthrough:
 * Suppose we have 4 elements `[A, B, C, D]` at indices `[0, 1, 2, 3]`.
 * We want to permute them using `target_indices = [3, 0, 1, 2]`, meaning:
 * - Index 0 receives element from index 3 ('D')
 * - Index 1 receives element from index 0 ('A')
 * - Index 2 receives element from index 1 ('B')
 * - Index 3 receives element from index 2 ('C')
 *
 * Expected result: `[D, A, B, C]`
 *
 * **Step-by-step cycle execution:**
 * 1. `start = 0`: Save `Slot[0]` ('A') into `temp = 'A'`. (Slot 0 is now a
 * hole).
 * 2. `next = target_indices[0] = 3`: Move `Slot[3]` ('D') -> `Slot[0]`. (Array:
 * `[D, B, C, D]`)
 * 3. `next = target_indices[3] = 2`: Move `Slot[2]` ('C') -> `Slot[3]`. (Array:
 * `[D, B, C, C]`)
 * 4. `next = target_indices[2] = 1`: Move `Slot[1]` ('B') -> `Slot[2]`. (Array:
 * `[D, B, B, C]`)
 * 5. `next = target_indices[1] = 0`: Since `next == start (0)`, close cycle by
 * placing `temp` ('A') -> `Slot[1]`.
 * 6. Final Array: `[D, A, B, C]`.
 *
 * All 4 elements are relocated in-place in O(N) operations using only 1 element
 * of temporary storage.
 *
 * @param arch Pointer to the target Archetype.
 * @param chunks Vector of chunks belonging to the archetype.
 * @param layout Column layout describing component element stride and offset.
 * @param target_indices Span defining the permutation: target_indices[i] =
 * source index for destination i.
 * @param visited Bitset tracking visited indices across permutation cycles.
 * @param temp Scratch byte buffer for holding a single component element during
 * cycle chasing.
 */
void permute_single_column(
    const Archetype *arch,
    const std::vector<Chunk *> &chunks,
    const ComponentColumnLayout &layout,
    std::span<const uint32_t> target_indices,
    std::vector<bool> &visited,
    std::vector<uint8_t> &temp
) {
  const size_t total = target_indices.size();
  const uint32_t size = layout.size;
  temp.resize(size);
  std::fill(visited.begin(), visited.end(), false);

  for (uint32_t start = 0; start < total; ++start) {
    // Skip elements already placed in previous cycles or elements that do not
    // move
    if (visited[start] || target_indices[start] == start) {
      continue;
    }

    // Save cycle head element into scratch buffer
    uint32_t cur = start;
    void *cur_ptr = resolve_component_ptr(arch, chunks, layout, cur);
    std::memcpy(temp.data(), cur_ptr, size);

    // Chase the cycle until returning to start
    while (true) {
      visited[cur] = true;
      uint32_t next = target_indices[cur];
      if (next == start) {
        // Complete the cycle by moving temp into the final position
        std::memcpy(cur_ptr, temp.data(), size);
        break;
      }
      void *next_ptr = resolve_component_ptr(arch, chunks, layout, next);
      std::memcpy(cur_ptr, next_ptr, size);
      cur = next;
      cur_ptr = resolve_component_ptr(arch, chunks, layout, cur);
    }
  }
}

/**
 * @brief Permutes all component columns of an archetype using in-place cycle
 * chasing.
 *
 * @param arch Pointer to the target Archetype.
 * @param chunks Vector of chunks belonging to the archetype.
 * @param layouts Span of component column layouts.
 * @param target_indices Span defining the permutation mapping.
 * @param visited Bitset used to track cycle traversal.
 */
void permute_all_component_columns(
    const Archetype *arch,
    const std::vector<Chunk *> &chunks,
    std::span<const ComponentColumnLayout> layouts,
    std::span<const uint32_t> target_indices,
    std::vector<bool> &visited
) {
  std::vector<uint8_t> temp_buffer;
  for (const auto &layout : layouts) {
    permute_single_column(
        arch, chunks, layout, target_indices, visited, temp_buffer
    );
  }
}

/**
 * @brief Permutes Entity handles across chunk slices and re-anchors SlotMap
 * records in-place.
 *
 * @details
 * Uses cycle chasing to permute 64-bit Entity handles. At each step of the
 * cycle, updates the SlotMap record for the moved entity, guaranteeing zero
 * dangling handles.
 *
 * @param arch Pointer to the target Archetype.
 * @param chunks Vector of chunks belonging to the archetype.
 * @param target_indices Span defining the permutation mapping.
 * @param slot_map World's SlotMap to synchronize with new chunk/row
 * coordinates.
 * @param visited Bitset tracking visited indices across permutation cycles.
 */
void permute_entities_and_sync_slots(
    const Archetype *arch,
    const std::vector<Chunk *> &chunks,
    std::span<const uint32_t> target_indices,
    SlotMap &slot_map,
    std::vector<bool> &visited
) {
  const size_t total = target_indices.size();
  std::fill(visited.begin(), visited.end(), false);
  Entity temp_entity;

  for (uint32_t start = 0; start < total; ++start) {
    if (visited[start] || target_indices[start] == start) {
      // Re-anchor SlotMap record for stationary entities if not updated
      Entity e = *resolve_entity_ptr(arch, chunks, start);
      sync_entity_slot(arch, chunks, slot_map, e, start);
      continue;
    }

    uint32_t cur = start;
    Entity *cur_ptr = resolve_entity_ptr(arch, chunks, cur);
    temp_entity = *cur_ptr;

    // Chase cycle for Entity handles
    while (true) {
      visited[cur] = true;
      uint32_t next = target_indices[cur];
      if (next == start) {
        *cur_ptr = temp_entity;
        sync_entity_slot(arch, chunks, slot_map, temp_entity, cur);
        break;
      }

      Entity *next_ptr = resolve_entity_ptr(arch, chunks, next);
      Entity moved = *next_ptr;
      *cur_ptr = moved;
      sync_entity_slot(arch, chunks, slot_map, moved, cur);

      cur = next;
      cur_ptr = resolve_entity_ptr(arch, chunks, cur);
    }
  }
}

} // namespace

// =============================================================================
// World: In-Place Cycle-Chasing Permutation Entry Point
// =============================================================================

/**
 * @brief Permutes all component columns and entity handles in an archetype
 * in-place.
 *
 * @details
 * ### Algorithm Overview:
 * 1. **Validation & Early-Out:** Verifies target_indices length matches entity
 * count; exits immediately if entity count is <= 1.
 * 2. **Component Cycle Chasing:** For each column layout, chases permutation
 * cycles using element-sized temporary buffers without allocating full columns.
 * 3. **Entity Routing Table Synchronization:** Re-arranges 64-bit Entity
 * handles in parallel with SlotMap records so external handle lookups remain
 * 100% stable.
 * 4. **Structural Dirty Marking:** Advances world tick and updates each
 * affected chunk's structure version so change-detection queries reflect the
 * permutation.
 *
 * @param arch Target archetype whose chunks will be permuted.
 * @param target_indices Permutation mapping of size equal to
 * arch->entity_count().
 */
void World::apply_permutation(
    Archetype *arch,
    std::span<const uint32_t> target_indices
) {
  // 1. Validation & Early Out
  assert(arch != nullptr);
  const size_t total = arch->entity_count();
  assert(
      target_indices.size() == total &&
      "Permutation size must match entity count"
  );

  if (total <= 1) {
    return;
  }

  // 2. Fetch chunk and column descriptors
  const auto &chunks = get_archetype_chunks(arch);
  const auto layouts = arch->column_layouts();

  // Visited bitset to track cycle-chasing progress across cycles (125 KB for 1M
  // entities)
  std::vector<bool> visited(total, false);

  // 3. Permute all component columns in-place using cycle chasing
  permute_all_component_columns(arch, chunks, layouts, target_indices, visited);

  // 4. Permute Entity handles and synchronize SlotMap routing table
  permute_entities_and_sync_slots(
      arch, chunks, target_indices, slot_map_, visited
  );

  // 5. Update structure version and mark columns dirty for change-detection
  // filters
  advance_tick();
  for (Chunk *chunk : chunks) {
    chunk->advance_structure_version(current_tick_);
  }
}

} // namespace arche
