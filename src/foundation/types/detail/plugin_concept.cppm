module;

#include <concepts>
#include <string_view>

export module arche:types.detail.plugin_concept;

export namespace arche {

class World;

namespace internal {
class Scheduler;
}

template <typename P>
concept Plugin = requires(P plugin, World &world, internal::Scheduler &scheduler) {
  { plugin.build(world, scheduler) } -> std::same_as<void>;
  { P::name() } -> std::convertible_to<std::string_view>;
};

} // namespace arche
