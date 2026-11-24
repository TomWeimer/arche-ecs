module;

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

export module arche:facade.system;

import :access;
import :types;
import :scheduler;
import :plugin;
import :thread_pool;

export namespace arche {

class World;

// =============================================================================
// Systems & Scheduler Domain Sub-Facade
// =============================================================================

/**
 * @class SystemFacade
 * @brief Domain sub-facade for registering execution stages, systems, and plugins.
 *
 * Provides high-level registration for simulation systems with declarative
 * access descriptors, compiles conflict-free parallel DAG execution stages, and
 * manages autonomous engine plugins.
 *
 * Accessible via `world.systems`.
 */
class SystemFacade {
public:
  constexpr SystemFacade() noexcept = default;
  explicit constexpr SystemFacade(
      Scheduler &scheduler,
      SystemExecutionTarget (*get_target)(void *ctx),
      PluginRegistry &plugins,
      void *world_ptr = nullptr
  ) noexcept
      : scheduler_(&scheduler),
        get_target_(get_target),
        plugins_(&plugins),
        world_ptr_(world_ptr) {}

  /// Executes all compiled scheduler stages in parallel using the provided ThreadPool.
  void run(ThreadPool &pool);

  /// Executes all compiled scheduler stages sequentially on the current thread.
  void run();

  /// Compiles system stages, resolving parallel dependencies and access descriptors.
  void compile();

  /// Adds an individual system function with its data access declaration.
  SystemFacade &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  );

  /// Direct mutable reference to the underlying stage Scheduler.
  [[nodiscard]] Scheduler &scheduler() noexcept;

  /// Direct read-only reference to the underlying stage Scheduler.
  [[nodiscard]] const Scheduler &scheduler() const noexcept;

  /// Registers a plugin name identifier for tracking and duplicate prevention.
  template <typename P>
  void register_plugin_name();

  /// Installs an autonomous Plugin struct with lifecycle initialization.
  template <Plugin P>
  World &add_plugin();

  template <Plugin P>
  World &add_plugin(P &&plugin);

  template <Plugin P>
  World &add_plugin(Scheduler &ext_scheduler);

  template <Plugin P>
  World &add_plugin(P &&plugin, Scheduler &ext_scheduler);

private:
  Scheduler *scheduler_{nullptr};
  SystemExecutionTarget (*get_target_)(void *ctx){nullptr};
  PluginRegistry *plugins_{nullptr};
  void *world_ptr_{nullptr};
};

} // namespace arche
