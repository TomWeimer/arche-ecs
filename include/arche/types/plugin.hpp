#pragma once

#include <concepts>
#include <string_view>

namespace arche {

class World;
class Scheduler;

// =============================================================================
// Plugin Concept
// =============================================================================

/**
 * @concept Plugin
 * @brief Compile-time interface for modular engine plugins and feature bundles.
 *
 * A valid plugin must implement:
 * 1. `static constexpr std::string_view name()`: Unique plugin identifier.
 * 2. `void build(World &world, Scheduler &scheduler)`: Configuration callback
 *    registering systems, components, and singleton resources.
 *
 * ```cpp
 * // Example:
 * struct PhysicsPlugin {
 *   static constexpr std::string_view name() { return "PhysicsPlugin"; }
 *   void build(World &world, Scheduler &scheduler) {
 *     world.insert_resource(Gravity{9.81f});
 *     scheduler.add_system(step_physics, Access{}.writes<Position,
 * Velocity>());
 *   }
 * };
 * ```
 */
template <typename P>
concept Plugin = requires(P plugin, World &world, Scheduler &scheduler) {
  // 1. Mandatory build entrypoint for registering systems and resources
  { plugin.build(world, scheduler) } -> std::same_as<void>;

  // 2. Mandatory unique plugin name for diagnostics and duplicate prevention
  { P::name() } -> std::convertible_to<std::string_view>;
};

} // namespace arche
