#pragma once

#include "arche/types/access.hpp"
#include "arche/types/plugin.hpp"
#include "arche/types/system.hpp"

#include <cstddef>
#include <string>

namespace arche {

class World;
class ThreadPool;
class Scheduler;

// =============================================================================
// Systems & Scheduler Domain Sub-Facade
// =============================================================================

/**
 * @class SystemFacade
 * @brief Domain sub-facade for registering execution stages, systems, and
 * plugins.
 *
 * Provides high-level registration for simulation systems with declarative
 * access descriptors, compiles conflict-free parallel DAG execution stages, and
 * manages autonomous engine plugins.
 *
 * Accessible via `world.systems`.
 *
 * ```cpp
 * // Example:
 * world.systems.add_system(
 *     "movement",
 *     Access{}.reads<Velocity>().writes<Position>(),
 *     movement_system,
 *     SystemScale::Wide
 * );
 * world.systems.compile();
 * world.systems.run(thread_pool);
 * world.systems.add_plugin<PhysicsPlugin>();
 * ```
 */
class SystemFacade {
public:
  explicit constexpr SystemFacade(World *world) noexcept : world_(world) {}

  /// Executes all compiled scheduler stages in parallel using the provided
  /// ThreadPool.
  void run(ThreadPool &pool);

  /// Executes all compiled scheduler stages sequentially on the current thread.
  void run();

  /// Compiles system stages, resolving parallel dependencies and access
  /// descriptors.
  void compile();

  /// Adds an individual system function with its data access declaration.
  SystemFacade &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  );

  /// Registers a plugin name identifier for tracking and duplicate prevention.
  template <typename P> void register_plugin_name();

  /// Direct mutable reference to the underlying stage Scheduler.
  [[nodiscard]] Scheduler &scheduler() noexcept;

  /// Direct read-only reference to the underlying stage Scheduler.
  [[nodiscard]] const Scheduler &scheduler() const noexcept;

  /// Installs an autonomous Plugin struct with lifecycle initialization.
  template <Plugin P> World &add_plugin();

  template <Plugin P> World &add_plugin(P &&plugin);

  template <Plugin P> World &add_plugin(Scheduler &ext_scheduler);

  template <Plugin P> World &add_plugin(P &&plugin, Scheduler &ext_scheduler);

private:
  World *world_{nullptr};
};

} // namespace arche
