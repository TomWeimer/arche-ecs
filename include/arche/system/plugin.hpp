#pragma once

#include <concepts>
#include <string_view>

/**
 * @file plugin.hpp
 * @brief Zero-overhead C++20 Concept defining modular engine plugins.
 *
 * @details
 * ## The Architectural Problem: Header Bloat & Monolithic Composition
 * In traditional game engine and ECS architectures, application entry points
 * (`main.cpp`) quickly degenerate into unmaintainable monoliths. Registering
 * dozens of systems, global singletons, and component types directly on the
 * central world requires including every internal header in the consumer
 * project. This causes:
 * 1. **Catastrophic compilation cascades**: Modifying a single internal
 *    component header forces recompilation of the entire application.
 * 2. **Fragile initialization order**: Systems and resources must be manually
 *    sequenced line by line in `main.cpp`, leading to subtle runtime bugs when
 *    dependencies change.
 * 3. **Poor reusability**: Engine subsystems (physics, audio, networking,
 *    rendering) cannot be neatly packaged, tested in isolation, or shared
 *    across multiple titles.
 *
 * ## The Solution: C++20 Compile-Time Plugins
 * `arche-ecs` solves this through the **Plugin Architecture**. A plugin is a
 * self-contained, cohesive module that encapsulates a specific domain's
 * resources, systems, and initial world state.
 *
 * ### Why a Concept Instead of an Interface?
 * In accordance with the `arche-ecs` core invariant (**Flat Data over
 * Abstraction**, zero `virtual` functions, zero vtables, `-fno-rtti` compliant),
 * plugins are NOT implemented using dynamic polymorphism or abstract base
 * classes (`class IPlugin`).
 *
 * Instead, `arche::Plugin` is a pure **C++20 Concept**. It enforces the
 * required interface entirely at compile time via static duck typing:
 * - **Zero runtime overhead**: No indirect vtable dispatch, no heap allocations
 *   for plugin instances.
 * - **Maximum inlining**: The compiler can inline the entire registration logic
 *   directly into the caller.
 * - **Clear compile-time diagnostics**: If a plugin fails to satisfy the contract,
 *   the compiler emits a concise constraint violation error at compile time
 *   rather than failing silently at runtime.
 *
 * ## How Plugins Preserve Modularity
 * 1. **Complete Domain Encapsulation**: A plugin's internal components, system
 *    functions, and mathematical helpers can reside inside an isolated `.cpp`
 *    file. Only the lightweight plugin struct itself needs to be exposed
 *    publicly.
 * 2. **Declarative Interleaving Across Plugins**: Plugins register their
 *    systems with declarative `AccessDescriptor` bitmasks. The `Scheduler`
 *    analyzes the unified Directed Acyclic Graph (DAG) across *all* installed
 *    plugins and automatically interleaves their systems into optimal, race-free
 *    parallel batches. Systems from `PhysicsPlugin` and `AnimationPlugin` can run
 *    concurrently without manual coordination between the two modules.
 * 3. **Stateless and Stateful Support**:
 *    - **Stateless Plugins**: Default-constructible structs installed via
 *      `world.add_plugin<CoreClockPlugin>()`.
 *    - **Stateful Plugins**: Parameterized structs with custom configuration
 *      options installed via `world.add_plugin(PhysicsPlugin{.gravity = -19.62f})`.
 * 4. **Duplicate Prevention & Diagnostics**: Mandatory `P::name()` returns a
 *    unique string identifier, enabling the `World` to detect and reject
 *    accidental duplicate plugin registrations before simulation begins.
 *
 * ### Example Usage:
 * ```cpp
 * // 1. Stateless Plugin Definition
 * struct CoreClockPlugin {
 *   static constexpr std::string_view name() noexcept { return "CoreClockPlugin"; }
 *
 *   void build(arche::World &world, arche::Scheduler &scheduler) const {
 *     world.insert_resource<SimulationClock>();
 *     scheduler.add_system(
 *         "TimeStep",
 *         arche::Access{}.writes_resource<SimulationClock>().build(),
 *         time_step_system,
 *         arche::SystemScale::Narrow
 *     );
 *   }
 * };
 *
 * // 2. Stateful Plugin Definition with Configuration
 * struct PhysicsPlugin {
 *   float gravity_y{-9.81f};
 *
 *   static constexpr std::string_view name() noexcept { return "PhysicsPlugin"; }
 *
 *   void build(arche::World &world, arche::Scheduler &scheduler) const {
 *     world.insert_resource<GravityConfig>(0.0f, gravity_y, 0.0f);
 *     scheduler.add_system(
 *         "Gravity",
 *         arche::Access{}.reads_resource<GravityConfig>().writes<Velocity>().build(),
 *         gravity_system,
 *         arche::SystemScale::Wide
 *     );
 *   }
 * };
 *
 * // 3. Ultra-Clean Application Entrypoint
 * int main() {
 *   arche::World world;
 *   arche::ThreadPool pool;
 *
 *   // Install plugins cleanly without importing internal system headers
 *   world.add_plugin<CoreClockPlugin>();
 *   world.add_plugin(PhysicsPlugin{.gravity_y = -19.62f});
 *
 *   // Compile DAG & run simulation
 *   world.scheduler().compile(world);
 *   for (int i = 0; i < 60; ++i) {
 *     world.run_systems(pool);
 *   }
 * }
 * ```
 */

namespace arche {

class World;
class Scheduler;

/**
 * @concept Plugin
 * @brief Compile-time concept defining the modular plugin contract for `arche-ecs`.
 *
 * @tparam P The plugin type to validate against the contract.
 *
 * @details
 * Any type `P` satisfies `Plugin` if and only if it provides:
 * 1. `plugin.build(world, scheduler)`: A member function taking mutable references
 *    to `World` and `Scheduler`, returning `void`. This serves as the initialization
 *    hook where the plugin registers its resources and systems.
 * 2. `P::name()`: A static member function returning a type convertible to
 *    `std::string_view`. Used for runtime diagnostics, logging, and duplicate
 *    prevention.
 */
template <typename P>
concept Plugin = requires(P plugin, World &world, Scheduler &scheduler) {
  // 1. Mandatory build entrypoint for registering systems and resources
  { plugin.build(world, scheduler) } -> std::same_as<void>;

  // 2. Mandatory unique plugin name for diagnostics and duplicate prevention
  { P::name() } -> std::convertible_to<std::string_view>;
};

} // namespace arche