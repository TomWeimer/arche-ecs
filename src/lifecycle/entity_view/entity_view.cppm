module;

#include <cassert>

export module arche:entity_view;

import :entity;

export namespace arche {

class World;

// =============================================================================
// Entity Handle Views (Const & Mutable)
// =============================================================================

/**
 * @class EntityRef
 * @brief Read-only lightweight view bound to a specific Entity handle and World instance.
 *
 * Provides safe, const-qualified component inspection and liveness queries without
 * requiring the caller to manually pass the World reference to every access.
 *
 * ```cpp
 * // Example:
 * EntityRef ref = world.entity(id);
 * if (ref.is_alive() && ref.has<Position>()) {
 *     const auto &pos = ref.get<Position>();
 * }
 * ```
 */
class EntityRef {
public:
  constexpr EntityRef() noexcept = default;
  constexpr EntityRef(const World *world, Entity entity) noexcept
      : world_(world), entity_(entity) {}

  /// Returns the underlying 64-bit Entity identifier.
  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }

  /// Explicit conversion to the raw Entity handle.
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }

  /// Returns true if the entity handle is currently alive and valid in the World.
  [[nodiscard]] bool is_alive() const noexcept;

  /// Returns true if the entity possesses component T.
  template <typename T> [[nodiscard]] bool has() const noexcept;

  /// Fast read-only component retrieval. Never marks chunk dirty.
  template <typename T> [[nodiscard]] const T &get() const;

private:
  const World *world_{nullptr};
  Entity entity_{null_entity};
};

/**
 * @class EntityMut
 * @brief Mutable lightweight view bound to an Entity handle and World instance.
 *
 * Enables chained mutation workflows: reading, writing, adding components,
 * removing components, and despawning the entity directly through the handle.
 *
 * ```cpp
 * // Example:
 * EntityMut mut = world.entity(id);
 * if (mut.is_alive()) {
 *     mut.get_mut<Position>().x += 10.0f;
 *     mut.add(Velocity{1.0f, 0.0f});
 * }
 * ```
 */
class EntityMut {
public:
  constexpr EntityMut() noexcept = default;
  constexpr EntityMut(World *world, Entity entity) noexcept
      : world_(world), entity_(entity) {}

  /// Implicit conversion to a read-only EntityRef view.
  [[nodiscard]] operator EntityRef() const noexcept {
    return EntityRef(world_, entity_);
  }

  /// Returns the underlying 64-bit Entity identifier.
  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }

  /// Explicit conversion to the raw Entity handle.
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }

  /// Returns true if the entity handle is alive.
  [[nodiscard]] bool is_alive() const noexcept;

  /// Checks if the entity possesses component T.
  template <typename T> [[nodiscard]] bool has() const noexcept;

  /// Read-only component inspection.
  template <typename T> [[nodiscard]] const T &get() const;

  /// Mutable component access. Automatically advances the chunk's dirty tick.
  template <typename T> [[nodiscard]] T &get_mut();

  /// Dynamically attaches component T to the entity.
  template <typename T> void add(T &&component);

  /// Dynamically removes component T from the entity.
  template <typename T> void remove();

  /// Despawns the entity, recycling its slot and updating the chunk via Swap-and-Pop.
  bool despawn();

private:
  World *world_{nullptr};
  Entity entity_{null_entity};
};

} // namespace arche
