module;

#include <cstdint>

export module arche:component_access;

import :types;
import :entity;
import :archetype;
import :chunk;
import :slot_map;

export namespace arche {

// =============================================================================
// Low-Level In-Place Component Access Contract
// =============================================================================

/**
 * @struct ComponentAccess
 * @brief High-performance in-place component element access and version stamping contract.
 *
 * Provides static primitives to inspect component presence, obtain read-only or
 * mutable typed references to component data within archetype chunks, and perform
 * column-level or archetype-wide mutation version stamping.
 *
 * Conforms strictly to Layer 3 dependency bounds: accepts only low-level storage
 * abstractions (`SlotMap`, `Archetype`, `Chunk`, `ArchetypeGraph`) with zero upward
 * coupling to `World`.
 */
struct ComponentAccess {
  /**
   * @brief Queries whether an entity currently possesses component type `T`.
   *
   * @tparam T Component type to query.
   * @param slot_map Generational entity-to-record mapping table.
   * @param entity Entity handle to test.
   * @return True if the entity is alive and its archetype contains `T`; false otherwise.
   */
  template <typename T>
  [[nodiscard]] static bool has(
      const SlotMap &slot_map,
      Entity entity
  ) noexcept;

  /**
   * @brief Retrieves an immutable reference to an entity's component of type `T`.
   *
   * @tparam T Component type to retrieve.
   * @param slot_map Generational entity-to-record mapping table.
   * @param entity Entity handle to inspect.
   * @return Const reference to the component instance in its archetype chunk.
   * @pre The entity must be alive and possess component `T`.
   */
  template <typename T>
  [[nodiscard]] static const T &get(
      const SlotMap &slot_map,
      Entity entity
  );

  /**
   * @brief Retrieves a mutable reference to an entity's component of type `T`
   * and stamps the owning chunk column version to `current_version`.
   *
   * @tparam T Component type to mutate.
   * @param slot_map Generational entity-to-record mapping table.
   * @param entity Entity handle to mutate.
   * @param current_version Current simulation version sequence for dirty tracking.
   * @return Mutable reference to the component instance in its archetype chunk.
   * @pre The entity must be alive and possess component `T`.
   */
  template <typename T>
  [[nodiscard]] static T &get_mut(
      const SlotMap &slot_map,
      Entity entity,
      uint32_t current_version
  );

  /**
   * @brief Stamps the column dirty tick of component `T` in a specific chunk.
   *
   * @tparam T Component type whose column version should be stamped.
   * @param arch Archetype describing the column layout.
   * @param chunk Chunk containing the component column.
   * @param current_version Simulation version sequence to stamp.
   */
  template <typename T>
  static void stamp_column_version(
      Archetype *arch,
      Chunk *chunk,
      uint32_t current_version
  ) noexcept;

  /**
   * @brief Stamps the column dirty tick of component `tid` across all archetypes and chunks.
   *
   * Used by system schedulers to mark entire component write-sets dirty following
   * batch execution.
   *
   * @param graph Archetype transition graph containing all active archetypes.
   * @param tid Type identifier of the component to stamp.
   * @param current_version Simulation version sequence to stamp.
   */
  static void stamp_component_version(
      const ArchetypeGraph &graph,
      TypeID tid,
      uint32_t current_version
  ) noexcept;
};

} // namespace arche
