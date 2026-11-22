#pragma once

#include "arche/command/command_buffer.hpp"
#include "arche/world.hpp"

namespace arche {

template <typename... Comps>
inline void CommandBuffer::spawn_entity_helper(World &world, const uint8_t *payload) {
  using TupleType = std::tuple<std::decay_t<Comps>...>;
  TupleType tuple{};
  if constexpr (sizeof...(Comps) > 0) {
    std::memcpy(static_cast<void *>(&tuple), payload, sizeof(TupleType));
  }
  std::apply(
      [&world](auto &&...items) {
        world.spawn(std::forward<decltype(items)>(items)...);
      },
      tuple
  );
}

template <typename T>
inline void CommandBuffer::register_type_helper(World &world) {
  internal::WorldAccess::registry(world).template register_type<T>();
}

} // namespace arche
