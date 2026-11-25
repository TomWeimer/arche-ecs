module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

export module arche:mutation;

export import :entity;
export import :component_id;
export import :slot_map;
export import :chunk_pool;
export import :archetype;
export import :archetype_graph;
export import :command;

export namespace arche::internal {

// =============================================================================
// Mutation Execution Context
// =============================================================================

/**
 * @struct MutationContext
 * @brief Non-owning execution context providing direct access to core ECS storage
 * facilities required for structural mutations.
 *
 * Inverts dependencies so that Layer 3 (Mutation) operates strictly on Layer 0-2
 * primitives without upward coupling or forward declarations to Layer 5 (World).
 */
struct MutationContext {
  SlotMap &slot_map;
  ChunkPool &chunk_pool;
  ArchetypeGraph &graph;
  ComponentRegistry &registry;
  uint32_t current_version{0};
};

// =============================================================================
// Mutation Engine
// =============================================================================

/**
 * @struct Mutation
 * @brief Authoritative engine for all immediate and deferred structural mutations.
 *
 * Centralizes entity allocation, deallocation, component addition, component
 * removal, and archetype table migration. Operates strictly on MutationContext,
 * ensuring zero upward dependencies and unifying mutation semantics across both
 * immediate World calls and deferred CommandBuffer playback.
 */
struct Mutation {
  /**
   * @brief Constructs a component in-place inside a specific chunk row slot.
   *
   * @tparam T Component type to construct.
   * @tparam U Forwarding argument type for component initialization.
   * @param arch Archetype defining the column layout.
   * @param chunk Target chunk page.
   * @param row Row index inside the chunk page.
   * @param value Component value to forward into the column slot.
   */
  template <typename T, typename U>
  static void construct_component_in_row(
      Archetype *arch,
      Chunk *chunk,
      uint32_t row,
      U &&value
  );

  /**
   * @brief Stamps the current mutation version onto a specific component column in a chunk.
   *
   * @tparam T Component type identifying the column.
   * @param ctx Mutation context supplying the current version sequence.
   * @param arch Archetype defining column offsets.
   * @param chunk Target chunk page.
   */
  template <typename T>
  static void stamp_column_version(
      MutationContext &ctx,
      Archetype *arch,
      Chunk *chunk
  ) noexcept;

  /**
   * @brief Updates SlotMap records for a contiguous batch of newly placed entities.
   *
   * @param slot_map Generation and location index mapping.
   * @param entities Slice of entity handles.
   * @param arch Destination archetype table.
   * @param chunk Destination chunk page.
   * @param start_row Starting row offset within the destination chunk page.
   */
  static void update_entity_locations(
      SlotMap &slot_map,
      std::span<const Entity> entities,
      Archetype *arch,
      Chunk *chunk,
      uint32_t start_row
  ) noexcept;

  /**
   * @brief Populates archetype chunk pages in batches across chunk page boundaries.
   *
   * @tparam ColumnWriterFn Functor with signature `(Chunk *chunk, uint32_t start_row, size_t placed, uint32_t to_add)`.
   * @param arch Destination archetype table.
   * @param pool Chunk memory pool for allocating pages.
   * @param slot_map SlotMap for registering entity handles to row locations.
   * @param entities Slices of entities being populated.
   * @param write_columns Callback populating column data.
   */
  template <typename ColumnWriterFn>
  static void populate_chunks_batch(
      Archetype *arch,
      ChunkPool &pool,
      SlotMap &slot_map,
      std::span<const Entity> entities,
      ColumnWriterFn &&write_columns
  );

  /**
   * @brief Spawns a single entity with an initial set of components.
   *
   * @tparam Comps Component types to attach to the new entity.
   * @param ctx Mutation context supplying storage and version facilities.
   * @param components Forwarding references to component values.
   * @return Entity Generational handle of the newly allocated entity.
   */
  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
  static Entity spawn(MutationContext &ctx, Comps &&...components);

  /**
   * @brief Deserializes a component tuple payload and instantiates an entity.
   *
   * Used primarily by CommandPlayback to apply deferred spawn commands.
   *
   * @tparam Comps Component types packed into the creation payload.
   * @param ctx Mutation context supplying storage and version facilities.
   * @param payload Raw byte pointer to the packed component tuple.
   * @return Entity Generational handle of the newly allocated entity.
   */
  template <typename... Comps>
  static void spawn_payload(MutationContext &ctx, const uint8_t *payload);

  /**
   * @brief Spawns a batch of entities sharing identical component values.
   *
   * @tparam Comps Component types to attach to the new entities.
   * @param ctx Mutation context supplying storage and version facilities.
   * @param batch Batch descriptor specifying the quantity of entities to spawn.
   * @param components Const references to prototype component values.
   * @return std::vector<Entity> Vector of allocated entity handles.
   */
  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  static std::vector<Entity> spawn_batch(
      MutationContext &ctx,
      Batch batch,
      const Comps &...components
  );

  /**
   * @brief Spawns a batch of entities with SoA column arrays and writes handles into an output buffer.
   *
   * @tparam Comps Component types to attach to the new entities.
   * @param ctx Mutation context supplying storage and version facilities.
   * @param batch Batch descriptor specifying the quantity of entities to spawn.
   * @param out_entities Output span receiving allocated entity handles.
   * @param column_data Pointers to contiguous arrays of component values for each column.
   */
  template <typename... Comps>
  static void spawn_batch(
      MutationContext &ctx,
      Batch batch,
      std::span<Entity> out_entities,
      const Comps *...column_data
  );

  /**
   * @brief Destroys a single entity and frees its chunk slot and SlotMap generation.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entity Target entity handle to destroy.
   * @return true if the entity existed and was successfully destroyed; false otherwise.
   */
  static bool destroy_entity(MutationContext &ctx, Entity entity);

  /**
   * @brief Destroys a batch of entities, grouping by chunk for bulk swap-and-pop efficiency.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entities Span of entity handles to destroy.
   */
  static void destroy_entities(
      MutationContext &ctx,
      std::span<const Entity> entities
  );

  /**
   * @brief Dynamically attaches a component to an entity by transitioning its archetype.
   *
   * @tparam T Component type to add.
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entity Target entity handle.
   * @param component Component instance to attach.
   */
  template <typename T>
  static void add_component(MutationContext &ctx, Entity entity, T &&component);

  /**
   * @brief Dynamically attaches a component (type-erased) to an entity by transitioning its archetype.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entity Target entity handle.
   * @param tid Type ID of the component being added.
   * @param data Pointer to the component's raw serialized data.
   */
  static void add_component(
      MutationContext &ctx,
      Entity entity,
      ComponentID tid,
      const void *data
  );

  /**
   * @brief Dynamically removes a component from an entity by transitioning its archetype.
   *
   * @tparam T Component type to remove.
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entity Target entity handle.
   */
  template <typename T>
  static void remove_component(MutationContext &ctx, Entity entity);

  /**
   * @brief Dynamically removes a component (type-erased) from an entity by transitioning its archetype.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entity Target entity handle.
   * @param tid Type ID of the component being removed.
   */
  static void remove_component(
      MutationContext &ctx,
      Entity entity,
      ComponentID tid
  );

  /**
   * @brief Migrates an entity's component columns across archetype chunks during structural mutation.
   *
   * Copies shared components from the source chunk to the destination chunk, relocates
   * or initialises mutated component columns, preserves column versions, performs
   * swap-and-pop on the source chunk, and updates SlotMap coordinates.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param entity Target entity handle being migrated.
   * @param old_record Location record before migration.
   * @param src_arch Source archetype table.
   * @param dst_arch Destination archetype table.
   * @param mutated_type ComponentID of the component being added or removed.
   * @param added_component_data Pointer to added component data (nullptr on removal).
   */
  static void migrate(
      MutationContext &ctx,
      Entity entity,
      const EntityRecord &old_record,
      Archetype *src_arch,
      Archetype *dst_arch,
      ComponentID mutated_type,
      const void *added_component_data
  );

  /**
   * @brief Applies all deferred mutation bytecode recorded in a CommandBuffer.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param cmd Target CommandBuffer containing recorded bytecode.
   */
  static void apply(MutationContext &ctx, CommandBuffer &cmd);

  /**
   * @brief Sequentially commits all per-thread CommandBuffers in a ParallelCommandBuffer.
   *
   * @param ctx Mutation context supplying storage and version facilities.
   * @param parallel_cmd ParallelCommandBuffer collection.
   */
  static void commit(MutationContext &ctx, ParallelCommandBuffer &parallel_cmd);
};

} // namespace arche
