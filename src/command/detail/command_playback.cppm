module;

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

export module arche:command.detail.command_playback;

import :command;
import :archetype;
import :archetype_graph;
import :chunk_pool;
import :type_id;
import :entity;
import :slot_map;
import :component_access;
import :archetype.detail.component_migration;

export namespace arche {

struct EntityRecord;

// =============================================================================
// CommandPlayback Execution Engine
// =============================================================================

/**
 * @struct CommandPlayback
 * @brief Internal execution engine applying deferred bytecode mutations
 * directly to ECS storage.
 *
 * Provides static execution helpers for deserializing and applying deferred
 * commands recorded in a CommandBuffer (entity creation, destruction, component
 * addition/removal) against the underlying Archetype chunks and SlotMap.
 */
struct CommandPlayback {
  /**
   * @brief Deserializes a component tuple payload and instantiates an entity
   * with its initial components.
   *
   * @tparam Comps Component types packed into the creation payload.
   * @param ctx Execution context exposing core ECS storage facilities.
   * @param payload Raw byte pointer to the packed component tuple.
   */
  template <typename... Comps>
  static void apply_create_entity(
      CommandExecutionContext &ctx,
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
      (ComponentAccess::construct_component_in_row<std::decay_t<Comps>>(
           arch, chunk, row, std::forward<decltype(items)>(items)
       ),
       ...);
    };
    std::apply(construct, tuple);

    // 7. Update SlotMap record with physical chunk location and increment
    // entity count.
    ctx.slot_map.update_location(entity, arch, chunk, row);
    arch->increment_entity_count(1);
  }

  /**
   * @brief Destroys a single entity and frees its chunk slot and SlotMap
   * generation.
   *
   * @param ctx Execution context.
   * @param entity Target entity handle to destroy.
   * @return true if the entity existed and was successfully destroyed; false
   * otherwise.
   */
  static bool apply_destroy_entity(
      CommandExecutionContext &ctx,
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

    // 4. Recycle empty chunks back to the pool if archetype has excess
    // capacity.
    if (chunk->is_empty() && arch->chunks().size() > 1) {
      arch->recycle_chunk(ctx.chunk_pool, chunk);
    }

    // 5. Free entity ID in SlotMap.
    ctx.slot_map.free(entity);
    return true;
  }

  /**
   * @brief Destroys a batch of entities, grouping by chunk for bulk
   * swap-and-pop efficiency.
   *
   * @param ctx Execution context.
   * @param entities Span of entity handles to destroy.
   */
  static void apply_destroy_entities(
      CommandExecutionContext &ctx,
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

    // 2. Perform bulk swap-and-pop on each affected chunk in descending row
    // order.
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

  /**
   * @brief Dynamically attaches a component to an entity by transitioning its
   * archetype.
   *
   * @param ctx Execution context.
   * @param entity Target entity handle.
   * @param tid Type ID of the component being added.
   * @param data Pointer to the component's raw serialized data.
   */
  static void apply_add_component(
      CommandExecutionContext &ctx,
      Entity entity,
      TypeID tid,
      const void *data
  ) {
    const EntityRecord *rec = ctx.slot_map.find_record(entity);
    if (!rec || !rec->archetype || rec->archetype->has_component(tid)) {
      return;
    }

    Archetype *dst = ctx.graph.traverse_add(rec->archetype, tid);
    migrate_entity(
        ctx.chunk_pool,
        ctx.slot_map,
        entity,
        *rec,
        rec->archetype,
        dst,
        tid,
        data
    );
  }

  /**
   * @brief Dynamically removes a component from an entity by transitioning its
   * archetype.
   *
   * @param ctx Execution context.
   * @param entity Target entity handle.
   * @param tid Type ID of the component being removed.
   */
  static void apply_remove_component(
      CommandExecutionContext &ctx,
      Entity entity,
      TypeID tid
  ) {
    const EntityRecord *rec = ctx.slot_map.find_record(entity);
    if (!rec || !rec->archetype || !rec->archetype->has_component(tid)) {
      return;
    }

    Archetype *dst = ctx.graph.traverse_remove(rec->archetype, tid);
    migrate_entity(
        ctx.chunk_pool,
        ctx.slot_map,
        entity,
        *rec,
        rec->archetype,
        dst,
        tid,
        nullptr
    );
  }

  /**
   * @brief Migrates an entity's component columns across archetype chunks
   * during structural mutation.
   */
  static void migrate_entity(
      ChunkPool &chunk_pool,
      SlotMap &slot_map,
      Entity entity,
      const EntityRecord &old_record,
      Archetype *src_arch,
      Archetype *dst_arch,
      TypeID mutated_type,
      const void *added_component_data
  ) {
    ComponentMigration::migrate(
        chunk_pool,
        slot_map,
        entity,
        old_record,
        src_arch,
        dst_arch,
        mutated_type,
        added_component_data
    );
  }
};

} // namespace arche
