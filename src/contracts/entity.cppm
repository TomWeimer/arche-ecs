module;

#include <cstddef>
#include <cstdint>
#include <optional>

export module arche:contracts.entity;

import :entity;

export namespace arche {

class World;

// =============================================================================
// Entities Domain Sub-Facade
// =============================================================================

/**
 * @class EntityFacade
 * @brief Domain sub-facade for global entity table queries and slot map
 * introspection.
 *
 * Encapsulates global entity lifecycle inspection, active population metrics,
 * allocation capacity, and slot index resolution.
 *
 * Accessible via `world.entities`.
 *
 * ```cpp
 * // Example:
 * if (world.entities.contains(e)) {
 *     size_t alive = world.entities.count();
 *     size_t cap   = world.entities.capacity();
 * }
 * auto maybe_e = world.entities.resolve(slot_index);
 * ```
 */
class EntityFacade {
public:
  explicit constexpr EntityFacade(World *world) noexcept : world_(world) {}

  /// Returns true if the entity handle is alive and matches its current
  /// generation.
  [[nodiscard]] bool contains(Entity entity) const noexcept;

  /// Returns the total number of living entities across all archetypes.
  [[nodiscard]] size_t count() const noexcept;

  /// Returns the allocated capacity of the internal slot map.
  [[nodiscard]] size_t capacity() const noexcept;

  /// Returns true if zero entities currently exist in the simulation.
  [[nodiscard]] bool is_empty() const noexcept;

  /// Reconstructs a valid Entity handle from a 32-bit slot index, or
  /// std::nullopt if dead/empty.
  [[nodiscard]] std::optional<Entity> resolve(uint32_t index) const noexcept;

private:
  World *world_{nullptr};
};

} // namespace arche
