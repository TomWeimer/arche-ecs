#include "arche/world.hpp"
#include "arche/hardware.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

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
  // 1. Check if the entity handle is valid and currently alive
  if (!slot_map_.is_alive(entity)) {
    return false;
  }

  // 2. Retrieve the entity's storage record (archetype, chunk, and row index)
  const EntityRecord &record = slot_map_.get_record(entity);
  Archetype *arch = record.archetype;
  Chunk *chunk = record.chunk;
  assert(arch != nullptr && chunk != nullptr);

  // 3. Swap the last entity into this row, pop it, and run destructors for its
  // components
  chunk->swap_and_pop(record.row, slot_map_, arch->column_layouts());
  arch->decrement_entity_count();

  // 4. Return chunk to the pool if it became completely empty and isn't the
  // sole chunk
  if (chunk->is_empty() && get_archetype_chunks(arch).size() > 1) {
    recycle_chunk(arch, chunk);
  }

  // 5. Invalidate and reclaim the entity handle in the slot map
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
  auto &chunks = get_archetype_chunks(arch);

  // 1. Acquire a new chunk buffer from the pool if current chunk is full or
  // none exist
  if (chunks.empty() || chunks.back()->is_full()) {
    void *raw = chunk_pool_.acquire_buffer();
    auto *chunk = Chunk::create(raw, arch, arch->chunk_capacity(), chunk_size_);
    chunk->set_chunk_index(static_cast<uint32_t>(chunks.size()));
    chunks.push_back(chunk);
  }

  // 2. Append the entity handle to the active chunk and return its location
  Chunk *active = chunks.back();
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
  // 1. Skip if entity was destroyed prior to command playback
  if (!is_alive(entity)) {
    return;
  }

  // 2. Skip if entity already contains this component
  const auto &rec = slot_map_.get_record(entity);
  if (!rec.archetype || rec.archetype->has_component(tid)) {
    return;
  }

  // 3. Follow graph transition edge (+tid) and migrate entity
  Archetype *dst = graph_.traverse_add(rec.archetype, tid);
  migrate_entity(entity, rec, rec.archetype, dst, tid, data);
}

/**
 * @brief Applies a deferred component removal command during playback.
 *
 * @param entity Entity handle to modify.
 * @param tid TypeID of the component being removed.
 */
void World::apply_command_remove_component(Entity entity, TypeID tid) {
  // 1. Skip if entity was destroyed prior to command playback
  if (!is_alive(entity)) {
    return;
  }

  // 2. Skip if entity does not possess this component
  const auto &rec = slot_map_.get_record(entity);
  if (!rec.archetype || !rec.archetype->has_component(tid)) {
    return;
  }

  // 3. Follow graph transition edge (-tid) and migrate entity
  Archetype *dst = graph_.traverse_remove(rec.archetype, tid);
  migrate_entity(entity, rec, rec.archetype, dst, tid, nullptr);
}

} // namespace arche
