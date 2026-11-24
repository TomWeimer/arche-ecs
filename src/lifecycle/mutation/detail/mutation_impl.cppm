module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

export module arche:mutation.detail.impl;

import :mutation;

export namespace arche::internal {

// =============================================================================
// In-Place Construction & Version Stamping Helpers
// =============================================================================

template <typename T, typename U>
inline void Mutation::construct_component_in_row(
    Archetype *arch,
    Chunk *chunk,
    uint32_t row,
    U &&value
) {
  // 1. Resolve column index for component type.
  const TypeID tid = type_id<std::decay_t<T>>();
  const int16_t col = arch->column_index(tid);
  assert(col >= 0 && "Component not present in archetype column layout");

  // 2. Locate target element slot in chunk column and construct in-place.
  const auto &layout = arch->column_layout(static_cast<size_t>(col));
  void *slot = chunk->component_element(layout, row);
  new (slot) std::decay_t<T>(std::forward<U>(value));
}

template <typename T>
inline void Mutation::stamp_column_version(
    MutationContext &ctx,
    Archetype *arch,
    Chunk *chunk
) noexcept {
  // 1. Find column index for component type and stamp current mutation tick.
  const int16_t col = arch->column_index(type_id<std::decay_t<T>>());
  if (col >= 0) {
    chunk->set_column_version(static_cast<uint16_t>(col), ctx.current_version);
  }
}

void Mutation::update_entity_locations(
    SlotMap &slot_map,
    std::span<const Entity> entities,
    Archetype *arch,
    Chunk *chunk,
    uint32_t start_row
) noexcept {
  // 1. Map each entity handle in the contiguous batch to its physical chunk row.
  for (size_t i = 0; i < entities.size(); ++i) {
    slot_map.update_location(
        entities[i], arch, chunk, start_row + static_cast<uint32_t>(i)
    );
  }
}

template <typename ColumnWriterFn>
inline void Mutation::populate_chunks_batch(
    Archetype *arch,
    ChunkPool &pool,
    SlotMap &slot_map,
    std::span<const Entity> entities,
    ColumnWriterFn &&write_columns
) {
  const size_t total_count = entities.size();
  size_t placed = 0;

  // 1. Fill available chunk page capacity iteratively until all entities are allocated.
  while (placed < total_count) {
    Chunk *chunk = arch->ensure_writable_chunk(pool);
    const uint32_t start_row = chunk->count();
    const uint32_t capacity = chunk->capacity();
    const uint32_t available = capacity - start_row;
    const uint32_t to_add = static_cast<uint32_t>(
        std::min<size_t>(available, total_count - placed)
    );

    auto slice = entities.subspan(placed, to_add);
    chunk->append_entities(slice);
    write_columns(chunk, start_row, placed, to_add);
    update_entity_locations(slot_map, slice, arch, chunk, start_row);

    placed += to_add;
  }
  // 2. Record aggregate entity count increase in archetype table.
  arch->increment_entity_count(total_count);
}

// =============================================================================
// Entity Lifecycle: Spawning
// =============================================================================

template <typename... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
inline Entity Mutation::spawn(MutationContext &ctx, Comps &&...components) {
  // 1. Ensure all component types are registered in the TypeRegistry.
  (ctx.registry.register_type<std::decay_t<Comps>>(), ...);

  // 2. Resolve destination archetype node in the graph.
  std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
  Archetype *arch = ctx.graph.get_or_create(types);

  // 3. Allocate generational entity handle in the SlotMap.
  Entity entity = ctx.slot_map.allocate();

  // 4. Allocate physical row slot in an archetype chunk.
  auto [chunk, row] = arch->allocate_slot(ctx.chunk_pool, entity);

  // 5. Construct components in the designated chunk column row.
  (construct_component_in_row<std::decay_t<Comps>>(
       arch, chunk, row, std::forward<Comps>(components)
   ),
   ...);

  // 6. Update entity count and SlotMap location.
  arch->increment_entity_count();
  ctx.slot_map.update_location(entity, arch, chunk, row);

  // 7. Update structural version and column modification ticks.
  chunk->set_structure_version(ctx.current_version);
  (stamp_column_version<std::decay_t<Comps>>(ctx, arch, chunk), ...);

  return entity;
}

template <typename... Comps>
inline void Mutation::spawn_payload(
    MutationContext &ctx,
    const uint8_t *payload
) {
  // 1. Deserialize component payload tuple from bytecode.
  using TupleType = std::tuple<std::decay_t<Comps>...>;
  TupleType tuple{};
  if constexpr (sizeof...(Comps) > 0) {
    std::memcpy(static_cast<void *>(&tuple), payload, sizeof(TupleType));
  }

  // 2. Ensure all component types are registered in the TypeRegistry.
  (ctx.registry.register_type<std::decay_t<Comps>>(), ...);

  // 3. Resolve destination archetype node in the graph.
  std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
  Archetype *arch = ctx.graph.get_or_create(types);

  // 4. Allocate generational entity handle in the SlotMap.
  Entity entity = ctx.slot_map.allocate();

  // 5. Allocate physical row slot in an archetype chunk.
  auto [chunk, row] = arch->allocate_slot(ctx.chunk_pool, entity);

  // 6. Construct components directly into the designated chunk column row.
  auto construct = [&](auto &&...items) {
    (construct_component_in_row<std::decay_t<Comps>>(
         arch, chunk, row, std::forward<decltype(items)>(items)
     ),
     ...);
  };
  std::apply(construct, tuple);

  // 7. Update SlotMap record with physical chunk location and increment entity count.
  ctx.slot_map.update_location(entity, arch, chunk, row);
  arch->increment_entity_count(1);

  // 8. Update structural and column versions.
  chunk->set_structure_version(ctx.current_version);
  (stamp_column_version<std::decay_t<Comps>>(ctx, arch, chunk), ...);
}

template <typename... Comps>
  requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
inline std::vector<Entity> Mutation::spawn_batch(
    MutationContext &ctx,
    Batch batch,
    const Comps &...components
) {
  const size_t count = batch.count;
  if (count == 0) {
    return {};
  }

  // 1. Register types and resolve destination archetype.
  (ctx.registry.register_type<std::decay_t<Comps>>(), ...);
  std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
  Archetype *arch = ctx.graph.get_or_create(types);

  // 2. Allocate batch of entity handles in SlotMap.
  std::vector<Entity> entities(count);
  ctx.slot_map.allocate_batch(count, entities.data());

  // 3. Populate chunks with uniform component values across chunk page boundaries.
  populate_chunks_batch(
      arch,
      ctx.chunk_pool,
      ctx.slot_map,
      entities,
      [&](Chunk *chunk, uint32_t start_row, size_t, uint32_t to_add) {
        (chunk->fill_column<std::decay_t<Comps>>(
             *arch->column_layout_for(type_id<std::decay_t<Comps>>()),
             start_row,
             to_add,
             components
         ),
         ...);
        chunk->set_structure_version(ctx.current_version);
        (stamp_column_version<std::decay_t<Comps>>(ctx, arch, chunk), ...);
      }
  );

  return entities;
}

template <typename... Comps>
inline void Mutation::spawn_batch(
    MutationContext &ctx,
    Batch batch,
    std::span<Entity> out_entities,
    const Comps *...column_data
) {
  const size_t count = batch.count;
  if (count == 0 || out_entities.size() < count) {
    return;
  }

  // 1. Register types and resolve destination archetype.
  (ctx.registry.register_type<std::decay_t<Comps>>(), ...);
  std::vector<TypeID> types = {type_id<std::decay_t<Comps>>()...};
  Archetype *arch = ctx.graph.get_or_create(types);

  // 2. Allocate batch of entity handles in SlotMap.
  ctx.slot_map.allocate_batch(count, out_entities.data());

  // 3. Populate chunks with SoA arrays across chunk page boundaries.
  populate_chunks_batch(
      arch,
      ctx.chunk_pool,
      ctx.slot_map,
      out_entities.subspan(0, count),
      [&](Chunk *chunk, uint32_t start_row, size_t offset, uint32_t to_add) {
        (chunk->copy_column<std::decay_t<Comps>>(
             *arch->column_layout_for(type_id<std::decay_t<Comps>>()),
             start_row,
             column_data + offset,
             to_add
         ),
         ...);
        chunk->set_structure_version(ctx.current_version);
        (stamp_column_version<std::decay_t<Comps>>(ctx, arch, chunk), ...);
      }
  );
}

// =============================================================================
// Entity Lifecycle: Destruction
// =============================================================================

bool Mutation::destroy_entity(
    MutationContext &ctx,
    Entity entity
) {
  // 1. Locate physical record in SlotMap.
  const EntityRecord *record = ctx.slot_map.find_record(entity);
  if (!record) {
    return false;
  }

  Archetype *arch = record->archetype;
  Chunk *chunk = record->chunk;
  assert(arch != nullptr && chunk != nullptr);

  // 2. Swap-and-pop the destroyed entity row to maintain chunk contiguity.
  const auto reloc = chunk->swap_and_pop(record->row, arch->column_layouts());
  if (reloc.has_relocation()) {
    ctx.slot_map.update_row(reloc.moved_entity, record->row);
  }
  arch->decrement_entity_count();

  // 3. Mark chunk structural modification version.
  chunk->set_structure_version(ctx.current_version);

  // 4. Recycle empty chunks back to the pool if archetype has excess capacity.
  if (chunk->is_empty() && arch->chunks().size() > 1) {
    arch->recycle_chunk(ctx.chunk_pool, chunk);
  }

  // 5. Free entity ID in SlotMap.
  ctx.slot_map.free(entity);
  return true;
}

void Mutation::destroy_entities(
    MutationContext &ctx,
    std::span<const Entity> entities
) {
  if (entities.empty()) {
    return;
  }

  // 1. Group row deletions per chunk.
  struct ChunkDeletions {
    Archetype *arch{nullptr};
    std::vector<uint32_t> rows;
  };
  std::unordered_map<Chunk *, ChunkDeletions> chunk_map;

  for (Entity e : entities) {
    const EntityRecord *rec = ctx.slot_map.find_record(e);
    if (!rec) {
      continue;
    }
    auto &entry = chunk_map[rec->chunk];
    entry.arch = rec->archetype;
    entry.rows.push_back(rec->row);
  }

  // 2. Perform bulk swap-and-pop on each affected chunk in descending row order.
  for (auto &[chunk, info] : chunk_map) {
    std::sort(info.rows.begin(), info.rows.end(), std::greater<uint32_t>());

    std::vector<EntityRelocation> relocations(info.rows.size());
    const uint32_t reloc_count = chunk->batch_swap_and_pop(
        info.rows, info.arch->column_layouts(), relocations
    );
    for (uint32_t i = 0; i < reloc_count; ++i) {
      ctx.slot_map.update_row(relocations[i].entity, relocations[i].new_row);
    }
    info.arch->decrement_entity_count(info.rows.size());

    chunk->set_structure_version(ctx.current_version);

    if (chunk->is_empty() && info.arch->chunks().size() > 1) {
      info.arch->recycle_chunk(ctx.chunk_pool, chunk);
    }
  }

  // 3. Free entity IDs in SlotMap in bulk.
  ctx.slot_map.free_batch(entities);
}

// =============================================================================
// Entity Lifecycle: Dynamic Component Addition & Removal
// =============================================================================

template <typename T>
inline void Mutation::add_component(
    MutationContext &ctx,
    Entity entity,
    T &&component
) {
  // 1. Register component type and find current entity location.
  ctx.registry.register_type<std::decay_t<T>>();
  const TypeID tid = type_id<std::decay_t<T>>();
  const EntityRecord *rec = ctx.slot_map.find_record(entity);
  if (!rec || !rec->archetype || rec->archetype->has_component(tid)) {
    return;
  }

  // 2. Traverse transition graph to resolve destination archetype.
  Archetype *dst = ctx.graph.traverse_add(rec->archetype, tid);

  // 3. Execute archetype migration.
  migrate(ctx, entity, *rec, rec->archetype, dst, tid, &component);
}

void Mutation::add_component(
    MutationContext &ctx,
    Entity entity,
    TypeID tid,
    const void *data
) {
  // 1. Find current entity record and ensure component is not already attached.
  const EntityRecord *rec = ctx.slot_map.find_record(entity);
  if (!rec || !rec->archetype || rec->archetype->has_component(tid)) {
    return;
  }

  // 2. Traverse transition graph to resolve destination archetype.
  Archetype *dst = ctx.graph.traverse_add(rec->archetype, tid);

  // 3. Execute archetype migration.
  migrate(ctx, entity, *rec, rec->archetype, dst, tid, data);
}

template <typename T>
inline void Mutation::remove_component(
    MutationContext &ctx,
    Entity entity
) {
  // 1. Find current entity record and ensure component is currently attached.
  const TypeID tid = type_id<std::decay_t<T>>();
  const EntityRecord *rec = ctx.slot_map.find_record(entity);
  if (!rec || !rec->archetype || !rec->archetype->has_component(tid)) {
    return;
  }

  // 2. Traverse transition graph to resolve destination archetype.
  Archetype *dst = ctx.graph.traverse_remove(rec->archetype, tid);

  // 3. Execute archetype migration.
  migrate(ctx, entity, *rec, rec->archetype, dst, tid, nullptr);
}

void Mutation::remove_component(
    MutationContext &ctx,
    Entity entity,
    TypeID tid
) {
  // 1. Find current entity record and ensure component is currently attached.
  const EntityRecord *rec = ctx.slot_map.find_record(entity);
  if (!rec || !rec->archetype || !rec->archetype->has_component(tid)) {
    return;
  }

  // 2. Traverse transition graph to resolve destination archetype.
  Archetype *dst = ctx.graph.traverse_remove(rec->archetype, tid);

  // 3. Execute archetype migration.
  migrate(ctx, entity, *rec, rec->archetype, dst, tid, nullptr);
}

// =============================================================================
// Entity Lifecycle: Archetype Migration
// =============================================================================

void Mutation::migrate(
    MutationContext &ctx,
    Entity entity,
    const EntityRecord &old_record,
    Archetype *src_arch,
    Archetype *dst_arch,
    TypeID mutated_type,
    const void *added_component_data
) {
  assert(src_arch != nullptr && dst_arch != nullptr);
  Chunk *src_chunk = old_record.chunk;
  const uint32_t src_row = old_record.row;

  // 1. Allocate destination slot in target archetype.
  auto [dst_chunk, dst_row] = dst_arch->allocate_slot(ctx.chunk_pool, entity);

  // 2. Relocate preserved components and write newly added component.
  for (size_t i = 0; i < dst_arch->component_count(); ++i) {
    const auto &dst_layout = dst_arch->column_layout(i);
    const TypeID tid = dst_layout.type_id;
    void *dst_ptr = dst_chunk->component_element(dst_layout, dst_row);

    if (tid == mutated_type && added_component_data) {
      std::memcpy(dst_ptr, added_component_data, dst_layout.size);
      dst_chunk->set_column_version(static_cast<uint16_t>(i), ctx.current_version);
      continue;
    }

    const int16_t src_col_idx = src_arch->column_index(tid);
    if (src_col_idx >= 0) {
      const auto &src_layout = src_arch->column_layout(
          static_cast<size_t>(src_col_idx)
      );
      void *src_ptr = src_chunk->component_element(src_layout, src_row);
      src_layout.descriptor->relocate(dst_ptr, src_ptr);
      dst_chunk->set_column_version(
          static_cast<uint16_t>(i),
          src_chunk->column_version(static_cast<uint16_t>(src_col_idx))
      );
    }
  }

  // 3. Swap-and-pop moved row from source chunk to maintain contiguity.
  const auto reloc = src_chunk->swap_and_pop(
      src_row, src_arch->column_layouts()
  );
  if (reloc.has_relocation()) {
    ctx.slot_map.update_row(reloc.moved_entity, src_row);
  }
  src_arch->decrement_entity_count();
  dst_arch->increment_entity_count();

  // 4. Update structural modification versions on both source and destination chunks.
  src_chunk->set_structure_version(ctx.current_version);
  dst_chunk->set_structure_version(ctx.current_version);

  // 5. Update entity coordinates in SlotMap.
  ctx.slot_map.update_location(entity, dst_arch, dst_chunk, dst_row);

  // 6. Recycle source chunk if empty and archetype holds multiple chunks.
  if (src_chunk->is_empty() && src_arch->chunks().size() > 1) {
    src_arch->recycle_chunk(ctx.chunk_pool, src_chunk);
  }
}

} // namespace arche
