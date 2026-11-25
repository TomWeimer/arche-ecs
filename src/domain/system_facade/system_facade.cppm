module;

#include <cassert>
#include <cstddef>
#include <string>

export module arche:domain.system;

import :scheduler;

export namespace arche {

// =============================================================================
// Individual System Domain Sub-Facade
// =============================================================================

/**
 * @class SystemFacade
 * @brief Domain sub-facade providing introspection and control over a single registered system.
 *
 * Accessible via `world.system("system_name")`.
 */
class SystemFacade {
public:
  constexpr SystemFacade() noexcept = default;
  explicit constexpr SystemFacade(
      internal::Scheduler &scheduler,
      size_t system_index,
      void *world_ptr = nullptr,
      internal::SystemExecutionTarget (*get_target)(void *ctx) = nullptr
  ) noexcept
      : scheduler_(&scheduler),
        system_index_(system_index),
        world_ptr_(world_ptr),
        get_target_(get_target) {}

  /// Returns whether this system is active in the scheduler.
  [[nodiscard]] bool is_enabled() const noexcept;

  /// Enables this system for subsequent world.run() iterations.
  void enable() noexcept;

  /// Disables this system (skipped during world.run()).
  void disable() noexcept;

  /// Explicitly runs this individual system immediately outside the main DAG.
  void run_now();

  /// Returns a formatted string displaying the data access of the system.
  [[nodiscard]] std::string show_access() const;

private:
  internal::Scheduler *scheduler_{nullptr};
  size_t system_index_{0};
  void *world_ptr_{nullptr};
  internal::SystemExecutionTarget (*get_target_)(void *ctx){nullptr};
};

} // namespace arche
