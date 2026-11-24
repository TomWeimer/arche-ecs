module;

#include <cstddef>
#include <cstdint>
#include <optional>

export module arche:facade.entity;

import :entity;
import :slot_map;

export namespace arche {

// =============================================================================
// Entities Domain Sub-Facade
// =============================================================================

/**
 * @class EntityFacade
 * @brief Domain sub-facade for global entity table queries and slot map introspection.
 *
 * Encapsulates global entity lifecycle inspection, active population metrics,
 * allocation capacity, and slot index resolution directly over the SlotMap storage.
 *
 * Accessible via `world.entities`.
 */
class EntityFacade {
public:
  constexpr EntityFacade() noexcept = default;
  explicit constexpr EntityFacade(SlotMap &slot_map) noexcept
      : slot_map_(&slot_map) {}

  /// Returns true if the entity handle is alive and matches its current generation.
  [[nodiscard]] bool contains(Entity entity) const noexcept;

  /// Returns the total number of living entities across all archetypes.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the allocated capacity of the internal slot map.
  [[nodiscard]] size_t capacity() const noexcept;

  /// Returns true if there are zero active entities in the simulation.
  [[nodiscard]] bool is_empty() const noexcept;

  /// Resolves an entity handle from a slot index, if alive.
  [[nodiscard]] std::optional<Entity> resolve(uint32_t index) const noexcept;

  /// Immutable reference to the underlying SlotMap routing table.
  [[nodiscard]] const SlotMap &slot_map() const noexcept;

  /// Mutable reference to the underlying SlotMap routing table.
  [[nodiscard]] SlotMap &slot_map() noexcept;

private:
  SlotMap *slot_map_{nullptr};
};

} // namespace arche
