module;

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

export module arche:domain.system;

import :access;
import :system_types;
import :scheduler;

export namespace arche {

namespace internal {
class ThreadPool;
}

class World;

// =============================================================================
// Systems & Scheduler Domain Sub-Facade
// =============================================================================

/**
 * @class SystemFacade
 * @brief Domain sub-facade for registering execution stages and systems.
 *
 * Provides high-level registration for simulation systems with declarative
 * access descriptors and compiles conflict-free parallel DAG execution stages.
 *
 * Accessible via `world.systems`.
 */
class SystemFacade {
public:
  constexpr SystemFacade() noexcept = default;
  explicit constexpr SystemFacade(
      internal::Scheduler &scheduler,
      internal::SystemExecutionTarget (*get_target)(void *ctx),
      internal::ThreadPool *pool = nullptr,
      void *world_ptr = nullptr
  ) noexcept
      : scheduler_(&scheduler),
        get_target_(get_target),
        pool_(pool),
        world_ptr_(world_ptr) {}

  /// Executes all compiled scheduler stages in parallel using the World's internal ThreadPool.
  void run();

  /// Executes all compiled scheduler stages sequentially on the calling thread.
  void run_sequential();

  /// Compiles system stages, resolving parallel dependencies and access descriptors.
  void compile();

  /// Adds an individual system function with its data access declaration.
  SystemFacade &add_system(
      std::string name,
      AccessDescriptor access,
      SystemFunction fn,
      SystemScale scale = SystemScale::Narrow
  );

private:
  internal::Scheduler *scheduler_{nullptr};
  internal::SystemExecutionTarget (*get_target_)(void *ctx){nullptr};
  internal::ThreadPool *pool_{nullptr};
  void *world_ptr_{nullptr};
};

} // namespace arche
