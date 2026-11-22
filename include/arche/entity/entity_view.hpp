#pragma once

#include "arche/entity/entity.hpp"
#include <cassert>
#include <type_traits>
#include <utility>

namespace arche {

class World;

/**
 * @class EntityRef
 * @brief Lightweight, read-only entity accessor handle for const World inspections.
 *
 * @details
 * Encapsulates an Entity handle and a const pointer to its owning World:
 * ```cpp
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

  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }
  [[nodiscard]] explicit constexpr operator Entity() const noexcept { return entity_; }

  [[nodiscard]] inline bool is_alive() const noexcept;

  template <typename T>
  [[nodiscard]] inline bool has() const noexcept;

  template <typename T>
  [[nodiscard]] inline const T &get() const;

private:
  const World *world_{nullptr};
  Entity entity_{null_entity};
};

/**
 * @class EntityMut
 * @brief Lightweight, mutable entity accessor handle for non-const World operations.
 *
 * @details
 * Encapsulates an Entity handle and a mutable pointer to its owning World:
 * ```cpp
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

  [[nodiscard]] constexpr Entity id() const noexcept { return entity_; }
  [[nodiscard]] explicit constexpr operator Entity() const noexcept { return entity_; }

  [[nodiscard]] inline bool is_alive() const noexcept;

  template <typename T>
  [[nodiscard]] inline bool has() const noexcept;

  template <typename T>
  [[nodiscard]] inline const T &get() const;

  template <typename T>
  [[nodiscard]] inline T &get_mut();

  template <typename T>
  inline void add(T &&component);

  template <typename T>
  inline void remove();

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

/// Type alias for backwards compatibility.
using EntityView = EntityMut;

} // namespace arche
