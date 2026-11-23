module;

#include <cstddef>

export module arche:contracts.resource;

import :types.detail.resource_id;

export namespace arche {

class World;

// =============================================================================
// Resources Domain Sub-Facade
// =============================================================================

/**
 * @class ResourceFacade
 * @brief Domain sub-facade for managing global singleton resources independent
 * of entities.
 *
 * Provides type-safe insertion, retrieval, and lifecycle management of ambient
 * world singletons stored in a flat monotonic array (zero RTTI, `-fno-rtti`
 * compliant).
 *
 * Accessible via `world.resources`.
 *
 * ```cpp
 * // Example:
 * world.resources.insert(Time{0.0f, 0.016f});
 * if (world.resources.has<Time>()) {
 *     Time &time = world.resources.get<Time>();
 *     time.delta_time = 0.033f;
 * }
 * world.resources.remove<Time>();
 * ```
 */
class ResourceFacade {
public:
  explicit constexpr ResourceFacade(World *world) noexcept : world_(world) {}

  /// Inserts or replaces a global singleton resource of type T.
  template <typename T, typename... Args> T &insert(Args &&...args);

  /// Retrieves a mutable reference to the singleton resource of type T.
  template <typename T> [[nodiscard]] T &get();

  /// Retrieves a read-only reference to the singleton resource of type T.
  template <typename T> [[nodiscard]] const T &get() const;

  /// Checks whether a singleton resource of type T exists.
  template <typename T> [[nodiscard]] bool has() const noexcept;

  /// Checks whether a singleton resource exists by its runtime ResourceID.
  [[nodiscard]] bool has(ResourceID id) const noexcept;

  /// Removes the singleton resource of type T, returning true if it was
  /// present.
  template <typename T> bool remove();

private:
  World *world_{nullptr};
};

} // namespace arche
