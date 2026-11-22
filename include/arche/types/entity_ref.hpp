#pragma once

#include "arche/types/entity.hpp"

#include <cassert>
#include <cstddef>

namespace arche {

class World;

// =============================================================================
// EntityRef: Read-Only Entity Accessor
// =============================================================================

/**
 * @class EntityRef
 * @brief Lightweight, read-only entity accessor handle for const World
 * inspections.
 *
 * Encapsulates an Entity handle and a const pointer to its owning World:
 * ```cpp
 * // Example:
 * const World &cworld = world;
 * auto e = cworld.entity(id);
 * if (e.is_alive() && e.has<Position>()) {
 *     const Position &pos = e.get<Position>();
 * }
 * ```
 */
class EntityRef {
public:
  constexpr EntityRef() noexcept = default;
  constexpr EntityRef(const World *world, Entity entity) noexcept
      : world_(world), entity_(entity) {}

  /// Returns the underlying 64-bit Entity handle.
  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }

  /// Explicit conversion to the underlying Entity handle.
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }

  /// Returns true if the entity is alive in the World and matches its current
  /// generation.
  [[nodiscard]] inline bool is_alive() const noexcept;

  /// Checks whether the entity possesses component T in O(1).
  template <typename T> [[nodiscard]] inline bool has() const noexcept;

  /// Retrieves a read-only reference to component T. Does not mark chunk dirty.
  template <typename T> [[nodiscard]] inline const T &get() const;

private:
  const World *world_{nullptr};
  Entity entity_{null_entity};
};

// =============================================================================
// EntityMut: Mutable Entity Accessor
// =============================================================================

/**
 * @class EntityMut
 * @brief Lightweight, mutable entity accessor handle for non-const World
 * operations.
 *
 * Encapsulates an Entity handle and a mutable pointer to its owning World:
 * ```cpp
 * // Example:
 * auto e = world.entity(id);
 * if (e.is_alive() && e.has<Position>()) {
 *     e.get_mut<Position>().x += 10.0f;
 *     e.add(Velocity{1.0f, 0.0f});
 *     e.remove<Velocity>();
 *     e.despawn();
 * }
 * ```
 */
class EntityMut {
public:
  constexpr EntityMut() noexcept = default;
  constexpr EntityMut(World *world, Entity entity) noexcept
      : world_(world), entity_(entity) {}

  /// Returns the underlying 64-bit Entity handle.
  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }

  /// Explicit conversion to the underlying Entity handle.
  [[nodiscard]] explicit constexpr operator Entity() const noexcept {
    return entity_;
  }

  /// Returns true if the entity is alive in the World and matches its current
  /// generation.
  [[nodiscard]] inline bool is_alive() const noexcept;

  /// Checks whether the entity possesses component T in O(1).
  template <typename T> [[nodiscard]] inline bool has() const noexcept;

  /// Retrieves a read-only reference to component T. Does not mark chunk dirty.
  template <typename T> [[nodiscard]] inline const T &get() const;

  /// Retrieves a mutable reference to component T, automatically dirtying the
  /// chunk.
  template <typename T> [[nodiscard]] inline T &get_mut();

  /// Dynamically attaches component T, migrating the entity to the target
  /// archetype.
  template <typename T> inline void add(T &&component);

  /// Dynamically removes component T, migrating the entity to the target
  /// archetype.
  template <typename T> inline void remove();

  /// Destroys the entity, freeing its slot and compacting the chunk via
  /// Swap-and-Pop.
  inline bool despawn();

  /// Implicit conversion from mutable handle to read-only handle.
  [[nodiscard]] inline operator EntityRef() const noexcept {
    return EntityRef(world_, entity_);
  }

  /// Explicit conversion to read-only handle.
  [[nodiscard]] inline EntityRef as_ref() const noexcept {
    return EntityRef(world_, entity_);
  }

private:
  World *world_{nullptr};
  Entity entity_{null_entity};
};

} // namespace arche
