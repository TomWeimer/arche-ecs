#pragma once

#include "arche/archetype/archetype.hpp"
#include "arche/chunk/chunk.hpp"
#include "arche/entity/entity.hpp"

namespace arche {

class World;

struct ComponentAccess {
  template <typename T>
  [[nodiscard]] static bool has(const World &world, Entity entity) noexcept;

  template <typename T>
  [[nodiscard]] static const T &get(const World &world, Entity entity);

  template <typename T>
  [[nodiscard]] static T &get_mut(World &world, Entity entity);

  template <typename T>
  static void add(World &world, Entity entity, T &&component);

  template <typename T>
  static void remove(World &world, Entity entity);

  template <typename T, typename U>
  static void construct_component_in_row(Archetype *arch, Chunk *chunk, uint32_t row, U &&value);

  template <typename T>
  static void stamp_column_version(World &world, Chunk *chunk) noexcept;

  static void stamp_component_version(World &world, TypeID tid) noexcept;
};

} // namespace arche
