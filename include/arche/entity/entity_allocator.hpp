#pragma once

#include "arche/archetype/archetype.hpp"
#include "arche/entity/entity.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace arche {

class World;

struct EntityAllocator {
  template <typename... Comps>
  static Archetype *resolve_archetype(World &world);

  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, Batch> || ...))
  static Entity spawn(World &world, Comps &&...components);

  template <typename... Comps>
    requires(!(std::is_same_v<std::decay_t<Comps>, std::span<Entity>> || ...))
  static std::vector<Entity> spawn(World &world, Batch batch, const Comps &...components);

  template <typename... Comps>
  static void spawn(World &world, Batch batch, std::span<Entity> out_entities, const Comps *...column_data);
};

} // namespace arche
